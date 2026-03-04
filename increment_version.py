import os

def increment_version(version_file):
    # Read the current version
    with open(version_file, "r") as file:
        version = file.read().strip()

    # Split the version into major, minor, and patch
    major, minor, patch = map(int, version.split("."))

    # Increment the patch version
    patch += 1

    # Generate the new version
    new_version = f"{major}.{minor}.{patch}"

    # Write the new version back to the file
    with open(version_file, "w") as file:
        file.write(new_version)

    return new_version

# Hook into PlatformIO's build process
def before_build(env):
    version_file = os.path.join(env["PROJECT_DIR"], "version.txt")
    if os.path.exists(version_file):
        new_version = increment_version(version_file)
        print(f"Updated firmware version to: {new_version}")
        env.Append(CPPDEFINES=[("FIRMWARE_VERSION", f'\\"{new_version}\\"')])

# Register the build script
Import("env")
before_build(env)