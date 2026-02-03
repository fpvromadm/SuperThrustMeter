Import("env")

def after_upload(source, target, env):
    env.Execute("pio run -t uploadfs")

env.AddPostAction("upload", after_upload)
