Import("env")


def upload_filesystem(source, target, env):
    env.Execute(
        "\"$PYTHONEXE\" -m platformio run -t uploadfs -e %s" % env["PIOENV"]
    )


env.AddPostAction("upload", upload_filesystem)
