"""
Cubemap utilities for converting equirectangular maps to cube faces.
"""

import numpy as np
from typing import Tuple, Dict


# Cube face definitions
FACE_NAMES = ['px', 'nx', 'py', 'ny', 'pz', 'nz']


def get_cube_face_directions(face: str, size: int) -> np.ndarray:
    """
    Generate direction vectors for each pixel in a cube face.
    
    Args:
        face: Face name ('px', 'nx', 'py', 'ny', 'pz', 'nz')
        size: Size of the cube face in pixels
        
    Returns:
        Array of shape (size, size, 3) containing normalized direction vectors
    """
    # Create UV coordinates from -1 to 1
    u = np.linspace(-1, 1, size)
    v = np.linspace(-1, 1, size)
    uu, vv = np.meshgrid(u, v)
    
    # Initialize direction array
    directions = np.zeros((size, size, 3), dtype=np.float32)
    
    if face == 'px':  # Positive X (right)
        directions[..., 0] = 1
        directions[..., 1] = -vv
        directions[..., 2] = -uu
    elif face == 'nx':  # Negative X (left)
        directions[..., 0] = -1
        directions[..., 1] = -vv
        directions[..., 2] = uu
    elif face == 'py':  # Positive Y (top)
        directions[..., 0] = uu
        directions[..., 1] = 1
        directions[..., 2] = vv
    elif face == 'ny':  # Negative Y (bottom)
        directions[..., 0] = uu
        directions[..., 1] = -1
        directions[..., 2] = -vv
    elif face == 'pz':  # Positive Z (front)
        directions[..., 0] = uu
        directions[..., 1] = -vv
        directions[..., 2] = 1
    elif face == 'nz':  # Negative Z (back)
        directions[..., 0] = -uu
        directions[..., 1] = -vv
        directions[..., 2] = -1
    
    # Normalize directions
    norms = np.linalg.norm(directions, axis=2, keepdims=True)
    directions = directions / (norms + 1e-8)
    
    return directions


def direction_to_equirectangular_uv(directions: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
    """
    Convert 3D direction vectors to equirectangular UV coordinates.
    
    Args:
        directions: Array of shape (..., 3) containing direction vectors
        
    Returns:
        Tuple of (u, v) arrays with values in [0, 1]
    """
    x = directions[..., 0]
    y = directions[..., 1]
    z = directions[..., 2]
    
    # Convert to spherical coordinates
    # theta: azimuth angle [-pi, pi]
    # phi: elevation angle [-pi/2, pi/2]
    theta = np.arctan2(z, x)
    phi = np.arcsin(np.clip(y, -1, 1))
    
    # Convert to UV coordinates [0, 1]
    u = (theta + np.pi) / (2 * np.pi)
    v = (phi + np.pi / 2) / np.pi
    
    return u, v


def sample_equirectangular(env_map: np.ndarray, u: np.ndarray, v: np.ndarray) -> np.ndarray:
    """
    Sample an equirectangular environment map using bilinear interpolation.
    
    Args:
        env_map: Environment map of shape (H, W, C)
        u: U coordinates in [0, 1]
        v: V coordinates in [0, 1]
        
    Returns:
        Sampled values with same shape as u/v plus channel dimension
    """
    height, width = env_map.shape[:2]
    
    # Convert UV to pixel coordinates
    x = u * (width - 1)
    y = (1 - v) * (height - 1)  # Flip V for image coordinates
    
    # Get integer coordinates for bilinear interpolation
    x0 = np.floor(x).astype(int)
    x1 = np.clip(x0 + 1, 0, width - 1)
    y0 = np.floor(y).astype(int)
    y1 = np.clip(y0 + 1, 0, height - 1)
    
    # Wrap x coordinates for seamless horizontal tiling
    x0 = x0 % width
    x1 = x1 % width
    
    # Clip y coordinates
    y0 = np.clip(y0, 0, height - 1)
    y1 = np.clip(y1, 0, height - 1)
    
    # Calculate interpolation weights
    wx = x - np.floor(x)
    wy = y - np.floor(y)
    
    # Expand dimensions for broadcasting
    wx = wx[..., np.newaxis]
    wy = wy[..., np.newaxis]
    
    # Bilinear interpolation
    result = (
        env_map[y0, x0] * (1 - wx) * (1 - wy) +
        env_map[y0, x1] * wx * (1 - wy) +
        env_map[y1, x0] * (1 - wx) * wy +
        env_map[y1, x1] * wx * wy
    )
    
    return result


def equirectangular_to_cubemap(env_map: np.ndarray, face_size: int) -> Dict[str, np.ndarray]:
    """
    Convert an equirectangular environment map to cubemap faces.
    
    Args:
        env_map: Environment map of shape (H, W, C)
        face_size: Size of each cube face in pixels
        
    Returns:
        Dictionary mapping face names to face images
    """
    cubemap = {}
    
    for face in FACE_NAMES:
        # Get direction vectors for this face
        directions = get_cube_face_directions(face, face_size)
        
        # Convert to equirectangular coordinates
        u, v = direction_to_equirectangular_uv(directions)
        
        # Sample the environment map
        face_image = sample_equirectangular(env_map, u, v)
        cubemap[face] = face_image.astype(env_map.dtype)
    
    return cubemap


# NOTE: sphere sampling helper removed — not used in current pipeline.


def generate_hemisphere_samples(num_samples: int) -> np.ndarray:
    """
    Generate cosine-weighted samples on a hemisphere.
    
    Args:
        num_samples: Number of samples
    Returns:
        Array of shape (num_samples, 3) containing direction vectors
    """
    # Generate random samples using cosine-weighted distribution (local RNG)
    rng = np.random.default_rng(42)
    u1 = rng.random(num_samples).astype(np.float32)
    u2 = rng.random(num_samples).astype(np.float32)
    
    # Cosine-weighted hemisphere sampling
    r = np.sqrt(u1)
    theta = 2 * np.pi * u2
    
    x = r * np.cos(theta)
    z = r * np.sin(theta)
    y = np.sqrt(np.maximum(0, 1 - u1))
    
    # Create tangent space samples
    samples = np.stack([x, y, z], axis=1)
    
    return samples


def rotate_to_normal(samples: np.ndarray, normal: np.ndarray) -> np.ndarray:
    """
    Rotate hemisphere samples to align with a given normal.
    
    Args:
        samples: Array of shape (N, 3) in tangent space (Y-up)
        normal: Target normal direction
        
    Returns:
        Rotated samples aligned with normal
    """
    normal = normal / np.linalg.norm(normal)
    
    # Build orthonormal basis
    up = np.array([0, 1, 0], dtype=np.float32)
    if abs(np.dot(normal, up)) > 0.999:
        up = np.array([1, 0, 0], dtype=np.float32)
    
    tangent = np.cross(up, normal)
    tangent_norm = np.linalg.norm(tangent)
    if tangent_norm <= 1e-8:
        tangent = np.array([1.0, 0.0, 0.0], dtype=np.float32)
        tangent_norm = 1.0
    tangent = tangent / tangent_norm
    bitangent = np.cross(normal, tangent)
    
    # Transform samples
    rotated = (
        samples[:, 0:1] * tangent +
        samples[:, 1:2] * normal +
        samples[:, 2:3] * bitangent
    )
    
    return rotated
