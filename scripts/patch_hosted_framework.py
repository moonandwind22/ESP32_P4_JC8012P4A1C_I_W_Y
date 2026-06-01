Import("env")

from pathlib import Path


def patch_hosted_framework():
    platform_packages = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    if not platform_packages:
        print("[londonbrief] framework-arduinoespressif32 package not found; skipping hosted patch")
        return

    hosted_file = Path(platform_packages) / "cores" / "esp32" / "esp32-hal-hosted.c"
    if not hosted_file.exists():
        print(f"[londonbrief] hosted framework file missing: {hosted_file}")
        return

    original = hosted_file.read_text(encoding="utf-8")
    marker = "/* LondonBrief patch: skip hostedHasUpdate() because version RPC wedges on this board. */"
    if marker in original:
        print("[londonbrief] hosted framework patch already present")
        return

    target = "    hostedHasUpdate();"
    replacement = f"    {marker}"
    if target not in original:
        print("[londonbrief] hostedHasUpdate() call not found; skipping hosted patch")
        return

    patched = original.replace(target, replacement, 1)
    hosted_file.write_text(patched, encoding="utf-8")
    print(f"[londonbrief] patched hosted framework: {hosted_file}")


patch_hosted_framework()
