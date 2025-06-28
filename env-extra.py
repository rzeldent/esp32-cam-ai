from os.path import isfile
Import("env")

if isfile(".env"):
    with open(".env", "r") as f:
        lines = f.readlines()
        flags = []
        for line in lines:
            key_value = line.strip().split("=", 1)
            if len(key_value) == 2:
                key, value = key_value
                flags.append(f'-D{key}="{value}"')
        env.Append(BUILD_FLAGS=flags)