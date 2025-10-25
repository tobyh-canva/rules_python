load("//python:py_executable_info.bzl", "PyExecutableInfo")


def zipapp_snapshot_impl(ctx):
    """Generate a snapshot of the manifest for a Python zipapp.
    
    Args:
        ctx: The context object.

    Returns:
        A DefaultInfo provider with the manifest snapshot file.
    """
    name = ctx.attr.name
    py_binary_target = ctx.attr.py_binary_target
    python_zip_file_outputs = py_binary_target[OutputGroupInfo].python_zip_file
    manifest_generator = ctx.executable._manifest_generator

    manifest_snapshot_file = ctx.actions.declare_file(name + "_manifest_snapshot.txt")
    args = ctx.actions.args()
    args.add_all(python_zip_file_outputs)
    args.add(manifest_snapshot_file)
    ctx.actions.run(
        executable = manifest_generator,
        inputs = python_zip_file_outputs,
        outputs = [manifest_snapshot_file],
        arguments = [args],
        mnemonic = "ZipappSnapshot",
        use_default_shell_env = True,
    )

    return [DefaultInfo(files = depset([manifest_snapshot_file]))]


zipapp_snapshot = rule(
    implementation = zipapp_snapshot_impl,
    attrs = {
        "py_binary_target": attr.label(mandatory = True, providers = [PyExecutableInfo]),
        "_manifest_generator": attr.label(
            executable = True,
            cfg = "exec",
            default = Label("//tests/bootstrap_impls/zipapp_snapshots:generate_manifest"),
        ),
    },
)
