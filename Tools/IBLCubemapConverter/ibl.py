"""
IBL (Image-Based Lighting) convolution for diffuse irradiance and specular maps.
"""

import numpy as np
from typing import Dict, Tuple, Generator
from cubemap import (
    FACE_NAMES, 
    get_cube_face_directions,
    direction_to_equirectangular_uv,
    sample_equirectangular,
    generate_hemisphere_samples,
    rotate_to_normal
)


def _geometry_smith_ggx_matching_shader(
    n_dot_l: np.ndarray,
    n_dot_v: float,
    alpha2: float,
    eps: float = 1e-6,
) -> np.ndarray:
    """
    Match GeometrySmithGGX implementation in Source/Rasterize/Shader.cpp.

    Returns G / (4 * N.L * N.V), same as renderer side.
    """
    n_dot_l = np.clip(n_dot_l, eps, 1.0)
    n_dot_v = float(np.clip(n_dot_v, eps, 1.0))
    n_dot_l2 = n_dot_l * n_dot_l
    n_dot_v2 = n_dot_v * n_dot_v
    ggxl = n_dot_l + np.sqrt(alpha2 + (1.0 - alpha2) * n_dot_l2)
    ggxv = n_dot_v + np.sqrt(alpha2 + (1.0 - alpha2) * n_dot_v2)
    return 1.0 / np.maximum(ggxl * ggxv, eps)


def compute_brdf_lut(
    size: int = 256,
    num_samples: int = 1024,
) -> np.ndarray:
    """
    Compute split-sum BRDF LUT that matches current converter GGX assumptions.

    LUT channels:
      R = scale (A), G = bias (B), B = 0

    Shader usage:
      specular = prefiltered * (F * A + B)

    Args:
        size: LUT resolution (size x size)
        num_samples: Monte Carlo samples per texel

    Returns:
        LUT image as float32 array of shape (size, size, 3)
    """
    print(f"Computing BRDF LUT ({size}x{size}, {num_samples} samples)...")

    n = np.array([0.0, 1.0, 0.0], dtype=np.float32)
    eps = 1e-6
    rng = np.random.default_rng(42)
    xi = rng.random((num_samples, 2), dtype=np.float32)
    lut = np.zeros((size, size, 3), dtype=np.float32)

    for y in range(size):
        roughness = (y + 0.5) / size
        alpha = max(roughness * roughness, 1e-4)
        alpha2 = alpha * alpha

        h_vectors = importance_sample_ggx(xi, roughness, n)

        for x in range(size):
            n_dot_v = (x + 0.5) / size
            n_dot_v = float(np.clip(n_dot_v, eps, 1.0))
            sin_theta_v = np.sqrt(max(1.0 - n_dot_v * n_dot_v, 0.0))
            v = np.array([sin_theta_v, n_dot_v, 0.0], dtype=np.float32)

            v_dot_h = np.sum(h_vectors * v, axis=1)
            l_vectors = 2.0 * v_dot_h[:, np.newaxis] * h_vectors - v

            n_dot_l = l_vectors[:, 1]
            n_dot_h = h_vectors[:, 1]

            valid = (n_dot_l > eps) & (v_dot_h > eps) & (n_dot_h > eps)
            if not np.any(valid):
                continue

            n_dot_l_v = n_dot_l[valid]
            n_dot_h_v = n_dot_h[valid]
            v_dot_h_v = v_dot_h[valid]

            term_v = _geometry_smith_ggx_matching_shader(
                n_dot_l_v, n_dot_v, alpha2, eps=eps)
            g_vis = (4.0 * n_dot_l_v * v_dot_h_v * term_v) / np.maximum(n_dot_h_v, eps)
            g_vis = np.maximum(g_vis, 0.0)

            fc = np.power(1.0 - np.clip(v_dot_h_v, 0.0, 1.0), 5.0)
            a = np.mean((1.0 - fc) * g_vis)
            b = np.mean(fc * g_vis)

            lut[y, x, 0] = float(a)
            lut[y, x, 1] = float(b)

    return lut


def compute_diffuse_irradiance(
    env_map: np.ndarray,
    face_size: int,
    num_samples: int = 2048
) -> Dict[str, np.ndarray]:
    """
    Compute diffuse irradiance cubemap by convolving environment map 
    with cosine-weighted hemisphere.
    
    Args:
        env_map: Equirectangular environment map (H, W, C)
        face_size: Size of output cube faces
        num_samples: Number of samples for Monte Carlo integration
        
    Returns:
        Dictionary of irradiance cube faces
    """
    print(f"Computing diffuse irradiance ({face_size}x{face_size}, {num_samples} samples)...")
    
    # Generate base hemisphere samples (Y-up tangent space)
    base_samples = generate_hemisphere_samples(num_samples)
    
    irradiance_map = {}
    
    for face_idx, face in enumerate(FACE_NAMES):
        print(f"  Processing face {face} ({face_idx + 1}/{len(FACE_NAMES)})...")
        
        # Get direction vectors for this face
        directions = get_cube_face_directions(face, face_size)
        
        # Initialize output
        face_irradiance = np.zeros((face_size, face_size, env_map.shape[2]), dtype=np.float32)
        
        # Process each pixel
        for y in range(face_size):
            for x in range(face_size):
                normal = directions[y, x]
                
                # Rotate samples to align with normal
                samples = rotate_to_normal(base_samples, normal)
                
                # Filter samples to hemisphere (N·L > 0)
                ndotl = np.sum(samples * normal, axis=1)
                valid_mask = ndotl > 0
                valid_samples = samples[valid_mask]
                if len(valid_samples) == 0:
                    continue
                
                # Sample environment map
                u, v = direction_to_equirectangular_uv(valid_samples)
                sampled_colors = sample_equirectangular(env_map, u, v)
                
                # Integrate with cosine weighting (cosine-weighted sampling PDF = cos(theta)/pi)
                # For cosine-weighted sampling the Monte Carlo estimator becomes:
                #   integral = pi * (1/N) * sum(Li)
                face_irradiance[y, x] = np.mean(sampled_colors, axis=0) * np.pi
        
        irradiance_map[face] = face_irradiance
    
    return irradiance_map


def importance_sample_ggx(
    xi: np.ndarray,
    roughness: float,
    normal: np.ndarray
) -> np.ndarray:
    """
    Generate GGX-distributed half vectors (importance samples) in world space.

    Args:
        xi: Random values of shape (N, 2), in [0,1]
        roughness: Surface roughness [0, 1]
        normal: Surface normal (3,)

    Returns:
        Array of half vectors of shape (N, 3) in world space (normalized).
    """
    alpha = max(roughness * roughness, 1e-4)
    alpha2 = alpha * alpha
    
    # GGX importance sampling
    phi = 2.0 * np.pi * xi[:, 0]
    cos_theta = np.sqrt((1.0 - xi[:, 1]) / (1.0 + (alpha2 - 1.0) * xi[:, 1]))
    sin_theta = np.sqrt(1.0 - cos_theta * cos_theta)
    
    # Spherical to Cartesian (tangent space)
    h_tangent = np.stack([
        sin_theta * np.cos(phi),
        cos_theta,
        sin_theta * np.sin(phi)
    ], axis=1)
    
    # Build TBN matrix robustly
    up = np.array([0.0, 1.0, 0.0], dtype=np.float32)
    if abs(float(np.dot(normal, up))) > 0.999:
        up = np.array([1.0, 0.0, 0.0], dtype=np.float32)

    tangent = np.cross(up, normal)
    tan_norm = np.linalg.norm(tangent)
    if tan_norm == 0:
        tangent = np.array([1.0, 0.0, 0.0], dtype=np.float32)
        tan_norm = 1.0
    tangent = tangent / tan_norm
    bitangent = np.cross(normal, tangent)
    
    # Transform to world space
    h = (
        h_tangent[:, 0:1] * tangent +
        h_tangent[:, 1:2] * normal +
        h_tangent[:, 2:3] * bitangent
    )
    
    # Normalize
    h = h / np.maximum(np.linalg.norm(h, axis=1, keepdims=True), 1e-8)
    
    return h


def compute_specular_map(
    env_map: np.ndarray,
    face_size: int,
    roughness: float,
    num_samples: int = 1024
) -> Dict[str, np.ndarray]:
    """
    Compute pre-filtered specular environment map for a given roughness.
    
    Args:
        env_map: Equirectangular environment map (H, W, C)
        face_size: Size of output cube faces
        roughness: Surface roughness [0, 1]
        num_samples: Number of samples for integration
        
    Returns:
        Dictionary of specular cube faces
    """
    print(f"Computing specular map (roughness={roughness:.2f}, {face_size}x{face_size})...")
    
    # Pre-generate random samples using a local RNG to avoid altering global state
    rng = np.random.default_rng(42)
    xi = rng.random((num_samples, 2), dtype=np.float32)
    
    specular_map = {}
    
    for face_idx, face in enumerate(FACE_NAMES):
        print(f"  Processing face {face} ({face_idx + 1}/{len(FACE_NAMES)})...")
        
        # Get direction vectors for this face
        directions = get_cube_face_directions(face, face_size)
        
        # Initialize output
        face_specular = np.zeros((face_size, face_size, env_map.shape[2]), dtype=np.float32)
        
        # Process each pixel
        for y in range(face_size):
            for x in range(face_size):
                normal = directions[y, x]
                view = normal  # Assume V = N for pre-filtering
                
                # Generate importance samples
                h_vectors = importance_sample_ggx(xi, roughness, normal)
                
                # Compute light directions (reflect view around half vector)
                # L = 2 * (V · H) * H - V
                vdoth = np.sum(view * h_vectors, axis=1, keepdims=True)
                l_vectors = 2.0 * vdoth * h_vectors - view
                
                # Filter valid light directions
                ndotl = np.sum(normal * l_vectors, axis=1)
                valid_mask = ndotl > 0
                
                if not np.any(valid_mask):
                    continue
                
                valid_l = l_vectors[valid_mask]
                valid_ndotl = ndotl[valid_mask]
                
                # Sample environment
                u, v = direction_to_equirectangular_uv(valid_l)
                sampled_colors = sample_equirectangular(env_map, u, v)
                
                # Weight by N·L and integrate
                weights = valid_ndotl[:, np.newaxis]
                total_weight = np.sum(valid_ndotl)
                
                if total_weight > 0:
                    face_specular[y, x] = np.sum(sampled_colors * weights, axis=0) / total_weight
        
        specular_map[face] = face_specular
    
    return specular_map


def generate_specular_mipmaps(
    env_map: np.ndarray,
    base_size: int,
    num_levels: int = 5,
    num_samples: int = 1024
) -> Generator[Tuple[int, Dict[str, np.ndarray], int, float], None, None]:
    """
    Generate specular cubemap mipmaps with increasing roughness.
    
    Args:
        env_map: Equirectangular environment map
        base_size: Size of the base mipmap level (level 0)
        num_levels: Number of mipmap levels to generate
        num_samples: Samples per pixel for integration
        
    Returns:
        List of cubemap dictionaries, one per mip level
    """
    for level in range(num_levels):
        # Calculate roughness for this level
        # Level 0 = roughness 0 (mirror), last level = roughness 1 (diffuse-like)
        roughness = level / max(num_levels - 1, 1)

        # Calculate face size for this mip level
        face_size = max(base_size >> level, 1)

        print(f"\nMipmap level {level}: roughness={roughness:.2f}, size={face_size}")

        # Compute specular map
        specular = compute_specular_map(env_map, face_size, roughness, num_samples)
        # Yield each level as it is computed so callers can save immediately
        yield level, specular, face_size, roughness



