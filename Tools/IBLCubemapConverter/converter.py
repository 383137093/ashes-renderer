"""
Core conversion logic for IBL cubemap generation.
"""

import os
import numpy as np
from typing import Optional, Dict, List

# Image I/O
import imageio.v3 as iio
# PIL not required here; imageio handles I/O

from cubemap import FACE_NAMES
from ibl import compute_diffuse_irradiance, generate_specular_mipmaps, compute_brdf_lut


ConversionResult = Dict[str, List[str]]


def calculate_luminance(image: np.ndarray) -> np.ndarray:
    """
    Calculate luminance using Rec.709 coefficients.
    
    Args:
        image: RGB image array (..., 3)
        
    Returns:
        Luminance array with same shape as input minus channel dimension
    """
    return 0.2126 * image[..., 0] + 0.7152 * image[..., 1] + 0.0722 * image[..., 2]


def load_environment_map(filepath: str) -> np.ndarray:
    """
    Load an environment map from file.
    
    Supports HDR, PNG, JPG, and other common formats.
    
    Args:
        filepath: Path to the environment map file
        
    Returns:
        Environment map as numpy array (H, W, C) in float32 format
    """
    ext = os.path.splitext(filepath)[1].lower()

    if ext in ('.hdr', '.exr'):
        # imageio's OpenCV plugin calls cv2.imread without IMREAD_ANYDEPTH,
        # which tone-maps HDR to uint8 and destroys physical float values.
        # Use cv2 directly with the correct flags to get true float32 radiance.
        import cv2
        bgr = cv2.imread(filepath, cv2.IMREAD_ANYDEPTH | cv2.IMREAD_COLOR)
        if bgr is None:
            raise RuntimeError(f"Failed to load HDR/EXR environment map: {filepath}")
        img = bgr[:, :, ::-1].astype(np.float32)  # BGR → RGB
    else:
        # Standard image formats (PNG, JPG, etc.)
        raw = iio.imread(filepath)
        orig_dtype = raw.dtype
        if orig_dtype == np.uint8:
            img = raw.astype(np.float32) / 255.0
        elif orig_dtype == np.uint16:
            img = raw.astype(np.float32) / 65535.0
        else:
            img = raw.astype(np.float32)
    
    # Ensure 3 channels
    if len(img.shape) == 2:
        img = np.stack([img, img, img], axis=2)
    elif img.shape[2] == 4:
        img = img[:, :, :3]  # Drop alpha

    print(f"Loaded environment map: {filepath}")
    print(f"  Shape: {img.shape}, dtype: {img.dtype}")
    print(f"  Value range: [{img.min():.4f}, {img.max():.4f}]")
    
    return img


def save_cubemap_face(
    face_data: np.ndarray,
    filepath: str,
    input_format: str
) -> None:
    """
    Save a single cubemap face to file.
    
    Args:
        face_data: Face image data (H, W, C)
        filepath: Output file path
        input_format: Original input format extension (e.g., '.hdr', '.png')
    """
    ext = input_format.lower()
    
    if ext == '.hdr':
        # Save as HDR
        iio.imwrite(filepath, face_data.astype(np.float32))
    elif ext == '.exr':
        # Save as EXR
        iio.imwrite(filepath, face_data.astype(np.float32))
    else:
        # Standard formats - convert to 8-bit or 16-bit
        if ext == '.png':
            # PNG can be 16-bit
            data = np.clip(face_data, 0, 1)
            data = (data * 65535).astype(np.uint16)
            iio.imwrite(filepath, data)
        else:
            # Other formats - 8-bit
            data = np.clip(face_data, 0, 1)
            data = (data * 255).astype(np.uint8)
            iio.imwrite(filepath, data)


def save_cubemap_separate(
    cubemap: Dict[str, np.ndarray],
    output_dir: str,
    base_name: str,
    output_format: str
) -> List[str]:
    """
    Save all faces of a cubemap as separate files.
    
    Args:
        cubemap: Dictionary mapping face names to image data
        output_dir: Output directory path
        base_name: Base filename (e.g., 'diffuse', 'specular0', 'specular1')
        output_format: Output format extension (e.g., '.hdr', '.png')
        
    Returns:
        List of saved file paths
    """
    os.makedirs(output_dir, exist_ok=True)
    saved_files = []
    
    for face_name in FACE_NAMES:
        face_data = cubemap[face_name]
        # Compute luminance stats for this face
        lum = calculate_luminance(face_data)
        print(f"    Face {face_name}: lum min={float(lum.min()):.6f}, max={float(lum.max()):.6f}, mean={float(lum.mean()):.6f}")
        filename = f"{base_name}_{face_name}{output_format}"
        filepath = os.path.join(output_dir, filename)
        save_cubemap_face(face_data, filepath, output_format)
        saved_files.append(filepath)
        print(f"  Saved: {filename}")
    
    return saved_files


def save_cubemap_layout(
    cubemap: Dict[str, np.ndarray],
    filepath: str,
    output_format: str,
    layout: str = 'vertical'
) -> str:
    """
    Save cubemap faces as a single image in various layouts.

    Args:
        cubemap: Dict mapping face names to image data
        filepath: Output file path (including extension)
        output_format: Output format extension (e.g., '.hdr', '.png')
        layout: Layout type: 'vertical', 'horizontal', or 'cross'

    Returns:
        The output filepath
    """
    # Ensure parent directory exists
    os.makedirs(os.path.dirname(filepath) if os.path.dirname(filepath) else '.', exist_ok=True)

    faces = [cubemap[name] for name in FACE_NAMES]
    face_h, face_w = faces[0].shape[0], faces[0].shape[1]
    
    if layout == 'horizontal':
        # Horizontal strip: px, nx, py, ny, pz, nz
        combined = np.concatenate(faces, axis=1)
    elif layout == 'vertical':
        # Vertical strip: px, nx, py, ny, pz, nz
        combined = np.concatenate(faces, axis=0)
    elif layout == 'cross':
        # Cross layout: 4x3 grid
        cw = face_w * 4
        ch = face_h * 3
        combined = np.zeros((ch, cw, faces[0].shape[2]), dtype=np.float32)
        # Mapping for cross layout:
        mapping = {
            (1, 0): 'py',
            (0, 1): 'nx',
            (1, 1): 'pz',
            (2, 1): 'px',
            (3, 1): 'nz',
            (1, 2): 'ny'
        }
        for (cx, cy), name in mapping.items():
            face = cubemap[name].astype(np.float32)
            y0 = cy * face_h
            x0 = cx * face_w
            combined[y0:y0+face_h, x0:x0+face_w] = face
    else:
        raise ValueError(f"Unknown layout: {layout}")
    
    # Save the combined image
    save_cubemap_face(combined, filepath, output_format)
    print(f"  Saved: {os.path.basename(filepath)}")
    
    return filepath


class IBLConverter:
    """
    Main converter class for generating IBL cubemaps from environment maps.
    """

    def __init__(
        self,
        diffuse_size: int = 64,
        specular_size: int = 256,
        num_mip_levels: int = 5,
        diffuse_samples: int = 2048,
        specular_samples: int = 1024,
    ):
        """
        Initialize the IBL converter.

        Args:
            diffuse_size: Size of diffuse irradiance cubemap faces
            specular_size: Size of base specular cubemap faces
            num_mip_levels: Number of specular mipmap levels
            diffuse_samples: Samples for diffuse convolution
            specular_samples: Samples for specular convolution
        """
        self.diffuse_size = diffuse_size
        self.specular_size = specular_size
        self.num_mip_levels = num_mip_levels
        self.diffuse_samples = diffuse_samples
        self.specular_samples = specular_samples

    def convert(
        self,
        input_path: str,
        output_dir: str,
        generate_diffuse: bool = True,
        generate_specular: bool = True,
        generate_brdf_lut: bool = False,
        brdf_lut_size: int = 256,
        brdf_lut_samples: int = 1024,
        output_format: Optional[str] = None,
        layout: str = 'vertical',
        radiance_clamp: Optional[float] = None,
        auto_clamp: bool = False,
        auto_clamp_factor: float = 2.0,
    ) -> ConversionResult:
        """
        Convert an environment map to IBL cubemaps.
        
        Args:
            input_path: Path to input environment map
            output_dir: Output directory
            generate_diffuse: Whether to generate diffuse irradiance map
            generate_specular: Whether to generate specular maps
            generate_brdf_lut: Whether to generate matching BRDF LUT
            brdf_lut_size: BRDF LUT resolution
            brdf_lut_samples: BRDF LUT Monte Carlo samples per texel
            output_format: Output format ('.hdr', '.png', etc.). If None, uses input format
            layout: Output layout ('separate', 'vertical', 'horizontal', 'cross')
            radiance_clamp: If set, clamp per-channel radiance values to this maximum
                before convolution. Eliminates Monte Carlo noise from extreme sun hotspots.
            auto_clamp: Automatically detect and clamp extreme hotspots. Computes P99.9
                luminance; if max > P99.9 * 10, clamps radiance to P99.9 * auto_clamp_factor.
                Ignored when radiance_clamp is already set.
            auto_clamp_factor: Multiplier applied to P99.9 to derive the auto clamp value.
                Higher values preserve more dynamic range; lower values are more aggressive.
                Default: 2.0.

        Returns:
            Dict with saved file paths for diffuse/specular/BRDF-LUT outputs
        """
        # Determine output format
        if output_format is None:
            output_format = os.path.splitext(input_path)[1]

        env_map = None
        need_env = generate_diffuse or generate_specular

        if need_env:
            # Load environment map
            env_map = load_environment_map(input_path)

            # Log source luminance statistics for diagnostics.
            src_lum = calculate_luminance(env_map)
            print(
                "Source luminance: "
                f"min={float(src_lum.min()):.6f}, "
                f"max={float(src_lum.max()):.6f}, "
                f"mean={float(src_lum.mean()):.6f}"
            )

            # Auto-detect extreme hotspots and derive a clamp value automatically.
            if auto_clamp and radiance_clamp is None:
                _trigger_ratio = 10.0
                p999 = float(np.percentile(src_lum, 99.9))
                if p999 > 0.0 and float(src_lum.max()) > p999 * _trigger_ratio:
                    radiance_clamp = p999 * auto_clamp_factor
                    print(
                        f"Auto-clamp triggered: max luma {float(src_lum.max()):.1f} "
                        f"> P99.9 {p999:.3f} × {_trigger_ratio:.0f}; "
                        f"clamping to P99.9 × {auto_clamp_factor} = {radiance_clamp:.3f}"
                    )

            # Clamp extreme radiance values (e.g. direct sun hotspots) that would
            # otherwise cause Monte Carlo noise and overexposure in the IBL maps.
            if radiance_clamp is not None and radiance_clamp > 0.0:
                clamped_pixels = int(np.any(env_map > radiance_clamp, axis=2).sum())
                env_map = np.clip(env_map, 0.0, radiance_clamp)
                print(
                    f"Radiance clamped to {radiance_clamp}: "
                    f"{clamped_pixels} pixels affected "
                    f"({100.0 * clamped_pixels / (env_map.shape[0] * env_map.shape[1]):.3f}%)"
                )

        # Convert equirectangular to cubemap
        base_size = max(self.specular_size, self.diffuse_size)
        if need_env:
            os.makedirs(output_dir, exist_ok=True)
            print(f"Converting equirectangular to cubemap ({base_size}x{base_size} per face)...")

        result: ConversionResult = {"diffuse": [], "specular": [], "brdf_lut": []}

        # Generate diffuse irradiance map
        if generate_diffuse:
            assert env_map is not None
            print("\n" + "=" * 50)
            print("Generating Diffuse Irradiance Map")
            print("=" * 50)

            irradiance = compute_diffuse_irradiance(env_map, self.diffuse_size, self.diffuse_samples)

            print("\nSaving diffuse irradiance cubemap...")
            if layout == 'separate':
                result["diffuse"] = save_cubemap_separate(irradiance, output_dir, "diffuse", output_format)
            else:
                filename = f"diffuse{output_format}"
                filepath = os.path.join(output_dir, filename)
                save_cubemap_layout(irradiance, filepath, output_format, layout)
                result["diffuse"].append(filepath)

        # Generate specular maps
        if generate_specular:
            assert env_map is not None
            print("\n" + "=" * 50)
            print("Generating Specular Maps with Mipmaps")
            print("=" * 50)

            for level, mipmap, face_size, roughness in generate_specular_mipmaps(
                env_map, self.specular_size, self.num_mip_levels, self.specular_samples
            ):
                print(f"\nSaving specular mipmap level {level} (size={face_size})...")
                if layout == 'separate':
                    files = save_cubemap_separate(mipmap, output_dir, f"specular{level}", output_format)
                    result["specular"].extend(files)
                else:
                    filename = f"specular{level}{output_format}"
                    filepath = os.path.join(output_dir, filename)
                    save_cubemap_layout(mipmap, filepath, output_format, layout)
                    result["specular"].append(filepath)

        if generate_brdf_lut:
            print("\n" + "=" * 50)
            print("Generating BRDF LUT")
            print("=" * 50)
            brdf_lut = compute_brdf_lut(size=brdf_lut_size, num_samples=brdf_lut_samples)
            if need_env:
                # Keep renderer-compatible location: sibling of IBL directory.
                brdf_path = os.path.join(output_dir, "..", "brdf_lut.hdr")
                brdf_path = os.path.normpath(brdf_path)
            else:
                # In BRDF-LUT-only mode, save directly in requested output dir.
                os.makedirs(output_dir, exist_ok=True)
                brdf_path = os.path.join(output_dir, "brdf_lut.hdr")
            iio.imwrite(brdf_path, brdf_lut.astype(np.float32))
            result["brdf_lut"].append(brdf_path)
            print(f"  Saved: {os.path.basename(brdf_path)}")

        return result
