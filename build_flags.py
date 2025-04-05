# coding: utf-8
import sys
import argparse

from teensy_minimal_rpc import __version__ as VERSION


def parse_args(sys_args=None):
    if sys_args is None:
        sys_args = sys.argv[1:]
    parser = argparse.ArgumentParser()

    default_version = VERSION
    parser.add_argument('-V', '--version', default=default_version)
    parser.add_argument('arg', nargs='*')

    return parser.parse_known_args(args=sys_args)


if __name__ == '__main__':
    args, extra_args = parse_args()

    print(' '.join(extra_args))
