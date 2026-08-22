# coding: utf-8
from platformio_helpers.upload import upload_conda
import platformio_helpers as pioh


if __name__ == '__main__':
    from argparse import ArgumentParser

    firmware_dir = pioh.conda_bin_path().joinpath('teensy-minimal-rpc')
    if not firmware_dir.isdir():
        raise IOError('No `teensy-minimal-rpc` firmware directory found at '
                      f'`{firmware_dir}`.  Has the firmware been built and '
                      'installed (e.g., `conda install teensy-minimal-rpc`)?')

    environments = sorted([dir_i.name for dir_i in firmware_dir.dirs()])
    if not environments:
        raise IOError('No firmware environments found in '
                      f'`{firmware_dir}`.  Expected at least one '
                      'sub-directory, one per hardware version.')

    parser = ArgumentParser(description='Upload firmware to board.')
    parser.add_argument('-p', '--port', default=None)
    parser.add_argument('-b', '--hardware-version', default=environments[-1],
                        choices=environments)
    args = parser.parse_args()
    extra_args = [] if args.port is None else ['--port', args.port]

    upload_conda('teensy-minimal-rpc',
                 env_name=args.hardware_version, extra_args=extra_args)
