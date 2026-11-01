Import("env")


def upload_filesystem(source, target, env):
    # Forward the port: without it an OTA upload's filesystem step falls back to
    # auto-detecting a serial port.
    port = env.subst("$UPLOAD_PORT")
    extra = (" --upload-port " + port) if port else ""
    env.Execute(
        "\"$PYTHONEXE\" -m platformio run -t uploadfs -e %s%s" % (env["PIOENV"], extra)
    )


env.AddPostAction("upload", upload_filesystem)
