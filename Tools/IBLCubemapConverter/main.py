#!/usr/bin/env python3
"""
IBL Cubemap Converter Tool

Converts equirectangular environment maps to IBL cubemaps including:
- Diffuse irradiance maps
- Specular reflection maps with mipmaps
- Optional BRDF LUT generation

Usage:
    python main.py sky.hdr -o output_folder
    python main.py sky.hdr --output-format png --layout horizontal
    python main.py sky.hdr --diffuse-size 128 --specular-size 512 --mip-levels 8
    python main.py --brdf-lut-only --generate-brdf-lut
"""

import argparse
import sys
from pathlib import Path
from typing import Optional

from converter import IBLConverter


# Confirmed in converter.load_environment_map: explicit HDR/EXR branch + standard PNG/JPG branch.
SUPPORTED_BATCH_FORMATS = {'.hdr', '.exr', '.png', '.jpg', '.jpeg'}


def print_separator():
    """Print a standard section separator."""
    print("=" * 60)


def resolve_output_dir(args, current_input: Optional[Path]) -> Path:
    """Resolve output directory for current input."""
    if current_input is None:
        if not args.output:
            print("Error: --brdf-lut-only requires an explicit output directory (-o)")
            sys.exit(1)
        return Path(args.output)
    if not args.output:
        return current_input.parent / 'ibl' / current_input.stem
    if args.batch:
        return Path(args.output) / current_input.stem
    return Path(args.output)


def resolve_output_format(args, current_input: Optional[Path]) -> str:
    """Resolve output file extension with leading dot."""
    if current_input is None:
        return '.hdr' if args.output_format == 'auto' else f".{args.output_format}"
    if args.output_format == 'auto':
        return current_input.suffix
    return f".{args.output_format}"


def get_generate_parts(generate_diffuse: bool, generate_specular: bool) -> str:
    """Return a readable generation mode label."""
    parts = []
    if generate_diffuse:
        parts.append('Diffuse')
    if generate_specular:
        parts.append('Specular')
    return ' + '.join(parts)


def resolve_generation_flags(args) -> tuple[bool, bool, bool]:
    """Resolve generation switches after mode overrides."""
    generate_diffuse = not args.specular_only
    generate_specular = not args.diffuse_only
    generate_brdf_lut = args.generate_brdf_lut or args.brdf_lut_only

    if args.brdf_lut_only:
        generate_diffuse = False
        generate_specular = False

    return generate_diffuse, generate_specular, generate_brdf_lut


def collect_input_files(args, input_path: Optional[Path]):
    """Collect and validate effective input list for current run mode."""
    if args.brdf_lut_only:
        return [None]

    if input_path is None:
        print("Error: Input is required unless --brdf-lut-only is used")
        sys.exit(1)

    if not input_path.exists():
        print(f"Error: Input file not found: {args.input}")
        sys.exit(1)

    if args.batch:
        if not (input_path.is_dir() or input_path.is_file()):
            print(f"Error: Invalid input path for batch mode: {args.input}")
            sys.exit(1)

        input_files = collect_batch_input_files(input_path)
        if not input_files:
            print("Error: No supported input files found for batch mode.")
            print(f"Supported formats: {', '.join(sorted(SUPPORTED_BATCH_FORMATS))}")
            sys.exit(1)
        return input_files

    if not input_path.is_file():
        print(f"Error: Input is not a file: {args.input}")
        print("Tip: Use --batch when input is a directory.")
        sys.exit(1)

    if input_path.suffix.lower() not in SUPPORTED_BATCH_FORMATS:
        print(f"Warning: Unrecognized format '{input_path.suffix}'. Attempting to load anyway...")

    return [input_path]


def print_conversion_config(
    args,
    current_input: Optional[Path],
    output_dir: Path,
    output_format: str,
    generate_diffuse: bool,
    generate_specular: bool,
    generate_brdf_lut: bool,
):
    """Print conversion configuration for one input file."""
    print_separator()
    print("IBL Cubemap Converter")
    print_separator()
    print(f"Input:           {current_input if current_input is not None else '<none>'}")
    print(f"Output:          {output_dir}")
    print(f"Output format:   {output_format[1:].upper()}")
    print(f"Output layout:   {args.layout}")
    print(f"Diffuse size:    {args.diffuse_size}x{args.diffuse_size}")
    print(f"Specular size:   {args.specular_size}x{args.specular_size}")
    print(f"Mipmap levels:   {args.mip_levels}")
    print(f"Diffuse samples: {args.diffuse_samples}")
    print(f"Specular samples: {args.specular_samples}")
    print(f"Generate:        {get_generate_parts(generate_diffuse, generate_specular)}")
    print(f"Generate BRDF LUT: {'Yes' if generate_brdf_lut else 'No'}")
    if generate_brdf_lut:
        print(f"BRDF LUT size:   {args.brdf_lut_size}x{args.brdf_lut_size}")
        print(f"BRDF LUT samples:{args.brdf_lut_samples}")
    if args.radiance_clamp is not None:
        print(f"Radiance clamp:  {args.radiance_clamp}")
    elif args.auto_clamp:
        print(f"Auto-clamp:      enabled (factor={args.auto_clamp_factor})")
    print_separator()


def print_conversion_result(result, output_dir: Path):
    """Print conversion output summary and return generated file count."""
    print("\n")
    print_separator()
    print("Conversion Complete!")
    print_separator()

    brdf_lut_files = result.get('brdf_lut', [])
    generated = len(result['diffuse']) + len(result['specular']) + len(brdf_lut_files)
    print(f"Total files generated: {generated}")

    if result['diffuse']:
        print(f"\nDiffuse irradiance ({len(result['diffuse'])} files):")
        for file_path in result['diffuse']:
            print(f"  - {Path(file_path).name}")

    if result['specular']:
        print(f"\nSpecular maps ({len(result['specular'])} files):")
        for file_path in sorted(result['specular']):
            print(f"  - {Path(file_path).name}")

    if brdf_lut_files:
        print(f"\nBRDF LUT ({len(brdf_lut_files)} files):")
        for file_path in brdf_lut_files:
            print(f"  - {Path(file_path).name}")

    print(f"\nOutput directory: {output_dir}")
    return generated


def parse_arguments():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description='Convert environment maps to IBL cubemaps and BRDF LUTs',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s sky.hdr
      Convert a single HDR file with default settings (output same format, horizontal strip)
      
  %(prog)s sky.hdr -o ./cubemaps --output-format png
      Convert a single HDR file to PNG format in horizontal strip layout
      
  %(prog)s sky.hdr --layout horizontal
      Output as horizontal strip instead of vertical
      
  %(prog)s sky.hdr --layout cross
      Output in cross layout (suitable for skybox)
      
  %(prog)s sky.hdr --layout separate
      Output as 6 separate face files (diffuse_px.hdr, diffuse_nx.hdr, etc.)
      
  %(prog)s sky.hdr --diffuse-size 128 --specular-size 512 --mip-levels 8
      Custom sizes and mipmap levels

  %(prog)s ./skies --batch -o ./cubemaps
      Batch convert all supported files in input directory ./skies (non-recursive)

    %(prog)s --brdf-lut-only --generate-brdf-lut -o ./ibl
            Generate only BRDF LUT to ./ibl/brdf_lut.hdr (output dir is required)

Output Layouts:
  separate:   6 files per map (diffuse_px.ext, diffuse_nx.ext, etc.)
    vertical:   Single vertical strip
    horizontal: Single horizontal strip (default)
  cross:      Cross layout for skybox
        """
    )
    
    parser.add_argument(
        'input',
        nargs='?',
        type=str,
        help='Input environment map file (HDR, PNG, JPG, etc.). Optional in --brdf-lut-only mode.'
    )
    
    parser.add_argument(
        '-o', '--output',
        type=str,
        default=None,
        help=(
            'Output directory. Default for file input: <input_dir>/ibl/<input_stem>; '
            'required for --brdf-lut-only mode.'
        )
    )
    
    parser.add_argument(
        '--diffuse-size',
        type=int,
        default=32,
        help='Size of diffuse irradiance cubemap faces (default: 32)'
    )
    
    parser.add_argument(
        '--specular-size',
        type=int,
        default=256,
        help='Size of base specular cubemap faces (default: 256)'
    )
    
    parser.add_argument(
        '--mip-levels',
        type=int,
        default=7,
        help='Number of specular mipmap levels (default: 7)'
    )
    
    parser.add_argument(
        '--diffuse-samples',
        type=int,
        default=2048,
        help='Number of samples for diffuse convolution (default: 2048)'
    )
    
    parser.add_argument(
        '--specular-samples',
        type=int,
        default=1024,
        help='Number of samples for specular convolution (default: 1024)'
    )
    
    parser.add_argument(
        '--diffuse-only',
        action='store_true',
        help='Generate only diffuse irradiance map'
    )
    
    parser.add_argument(
        '--specular-only',
        action='store_true',
        help='Generate only specular maps'
    )

    parser.add_argument(
        '--generate-brdf-lut',
        action='store_true',
        help='Generate BRDF LUT compatible with generated prefilter maps'
    )

    parser.add_argument(
        '--brdf-lut-size',
        type=int,
        default=256,
        help='BRDF LUT size (default: 256)'
    )

    parser.add_argument(
        '--brdf-lut-samples',
        type=int,
        default=1024,
        help='Number of samples per BRDF LUT texel (default: 1024)'
    )

    parser.add_argument(
        '--brdf-lut-only',
        action='store_true',
        help='Generate only BRDF LUT (does not require input map)'
    )
    
    parser.add_argument(
        '--output-format',
        type=str,
        default='auto',
        choices=['auto', 'hdr', 'png'],
        help='Output file format: auto (same as input), hdr, or png (default: auto)'
    )
    
    parser.add_argument(
        '--layout',
        type=str,
        default='horizontal',
        choices=['separate', 'vertical', 'horizontal', 'cross'],
        help='Output layout: separate (6 files), vertical strip, horizontal strip, or cross (default: horizontal)'
    )

    parser.add_argument(
        '--batch',
        action='store_true',
        help='Batch mode: process supported files in input directory (non-recursive)'
    )

    parser.add_argument(
        '--radiance-clamp',
        type=float,
        default=None,
        help=(
            'Clamp per-channel radiance values in the source HDR to this maximum before '
            'convolution. Eliminates Monte Carlo noise and overexposure caused by '
            'extremely bright sun hotspots (e.g. --radiance-clamp 64). '
            'Default: no clamping.'
        )
    )

    parser.add_argument(
        '--auto-clamp',
        action='store_true',
        help=(
            'Automatically detect and clamp extreme sun hotspots before convolution. '
            'Triggers when max luminance > P99.9 luminance × 10. '
            'Clamp value = P99.9 × --auto-clamp-factor. '
            'Ignored when --radiance-clamp is specified.'
        )
    )

    parser.add_argument(
        '--auto-clamp-factor',
        type=float,
        default=2.0,
        metavar='FACTOR',
        help=(
            'Multiplier applied to P99.9 luminance to derive the auto-clamp threshold. '
            'Higher = more dynamic range preserved; lower = more aggressive clamping. '
            'Default: 2.0'
        )
    )

    return parser.parse_args()


def collect_batch_input_files(input_path: Path):
    """Collect batch input files from a directory (top-level only)."""
    if input_path.is_file():
        return [input_path]

    files = []
    skipped = 0

    for p in sorted(input_path.iterdir(), key=lambda x: x.name.lower()):
        if not p.is_file():
            continue
        if p.suffix.lower() in SUPPORTED_BATCH_FORMATS:
            files.append(p)
        else:
            skipped += 1

    if skipped > 0:
        print(f"Skipped unsupported files: {skipped}")

    return files


def main():
    """Main entry point."""
    args = parse_arguments()

    if args.batch and args.brdf_lut_only:
        print("Error: --batch cannot be used with --brdf-lut-only")
        sys.exit(1)

    if args.diffuse_only and args.specular_only:
        print("Error: Cannot use both --diffuse-only and --specular-only")
        sys.exit(1)

    input_path = Path(args.input) if args.input is not None else None
    input_files = collect_input_files(args, input_path)

    generate_diffuse, generate_specular, generate_brdf_lut = resolve_generation_flags(args)

    # Create converter and run
    converter = IBLConverter(
        diffuse_size=args.diffuse_size,
        specular_size=args.specular_size,
        num_mip_levels=args.mip_levels,
        diffuse_samples=args.diffuse_samples,
        specular_samples=args.specular_samples
    )

    if args.batch:
        print_separator()
        print(f"Batch mode enabled: {len(input_files)} file(s) (non-recursive)")
        print(f"Supported formats: {', '.join(sorted(SUPPORTED_BATCH_FORMATS))}")
        print_separator()

    success_count = 0
    fail_count = 0
    total_files_generated = 0

    for idx, current_input in enumerate(input_files, start=1):
        output_dir = resolve_output_dir(args, current_input)
        current_output_format = resolve_output_format(args, current_input)

        if args.batch:
            print(f"\n[{idx}/{len(input_files)}] {current_input}")

        print_conversion_config(
            args,
            current_input,
            output_dir,
            current_output_format,
            generate_diffuse,
            generate_specular,
            generate_brdf_lut,
        )

        try:
            result = converter.convert(
                "" if current_input is None else str(current_input),
                str(output_dir),
                generate_diffuse=generate_diffuse,
                generate_specular=generate_specular,
                generate_brdf_lut=generate_brdf_lut,
                brdf_lut_size=args.brdf_lut_size,
                brdf_lut_samples=args.brdf_lut_samples,
                output_format=current_output_format,
                layout=args.layout,
                radiance_clamp=args.radiance_clamp,
                auto_clamp=args.auto_clamp,
                auto_clamp_factor=args.auto_clamp_factor,
            )

            generated = print_conversion_result(result, output_dir)
            total_files_generated += generated
            success_count += 1

        except Exception as e:
            fail_count += 1
            print(f"\nError during conversion: {e}")
            import traceback
            traceback.print_exc()

            # Keep processing remaining files in batch mode.
            if not args.batch:
                sys.exit(1)

    if args.batch:
        print("\n")
        print_separator()
        print("Batch Summary")
        print_separator()
        print(f"Total inputs:         {len(input_files)}")
        print(f"Succeeded:            {success_count}")
        print(f"Failed:               {fail_count}")
        print(f"Total files generated:{total_files_generated}")

    if fail_count > 0:
        return 1
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
