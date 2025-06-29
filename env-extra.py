from os.path import isfile
Import("env")

env_file = ".env" if isfile(".env") else ".env.template"

with open(env_file, "r") as f:
    lines = f.readlines()
    flags = []
    for line in lines:
        key_value = line.strip().split("=", 1)
        if len(key_value) == 2:
            key, value = key_value
            flags.append(f'-D{key}="{value}"')
    env.Append(BUILD_FLAGS=flags)