import subprocess

Import("env")

try:
    version = subprocess.check_output(
        ["git", "describe", "--tags", "--always", "--dirty"]
    ).decode().strip()
except (subprocess.CalledProcessError, OSError):
    version = "unknown"

env.Append(BUILD_FLAGS=['-DFW_VERSION=\\"%s\\"' % version])
