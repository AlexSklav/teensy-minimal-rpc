# coding: utf-8
import platform

import conda_helpers as ch

from typing import List, Dict

from path_helpers import path

try:
    from .config import Config, State
except (ImportError, TypeError):
    pass

from .proxy import Proxy, I2cProxy, SerialProxy

from ._version import get_versions

__version__ = get_versions()['version']
del get_versions


def conda_arduino_include_path():
    if platform.system() in ('Linux', 'Darwin'):
        return ch.conda_prefix().joinpath('include', 'Arduino')
    elif platform.system() == 'Windows':
        return ch.conda_prefix().joinpath('Library', 'include', 'Arduino')
    raise f'Unsupported platform: {platform.system()}'


def package_path() -> path:
    return path(__file__).parent


def get_sketch_directory() -> path:
    """
    Return directory containing the Arduino sketch.
    """
    return package_path().joinpath('..', 'src').realpath()


def get_lib_directory() -> path:
    return package_path().joinpath('..', 'lib').realpath()


def get_includes() -> List[path]:
    """
    Return directories containing the Arduino header files.

    Notes
    =====

    For example:

        import arduino_rpc
        ...
        print ' '.join(['-I%s' % i for i in arduino_rpc.get_includes()])
        ...

    """
    return list(conda_arduino_include_path().walkdirs('src'))


def get_sources() -> List[path]:
    """
    Return Arduino source file paths.  This includes any supplementary source
    files that are not contained in Arduino libraries.
    """
    import base_node_rpc

    return (get_sketch_directory().files('*.c*') +
            list(get_lib_directory().walkfiles('*.c*')) +
            base_node_rpc.get_sources())


def get_firmwares() -> Dict:
    """
    Return compiled Arduino hex file paths.

    This function may be used to locate firmware binaries that are available
    for flashing to [Arduino][1] boards.

    [1]: http://arduino.cc
    """
    return {board_dir.name: [f.abspath() for f in board_dir.walkfiles('*.hex')]
            for board_dir in package_path().joinpath('firmware').dirs()}
