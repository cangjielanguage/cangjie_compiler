#!/usr/bin/env python3
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# Licensed under Apache-2.0 with Runtime Library Exception.
"""Build the fixed ArkTS SDK with CJPM in a fresh external Windows work directory.

The SDK retains one ABI-1 context provider. Applications consume the resulting
CJO files and all three ordinary dynamic libraries; no application source is
generated and no installed SDK file is modified.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


PRODUCTS = (
    "arkts/arkts.cjo", "arkts/arkts.lang.cjo", "arkts/libarkts.so",
    "arkts/libarkts.lang.so", "cj_arkts_context_v1/cj_arkts_context_v1.cjo",
    "cj_arkts_context_v1/libcj_arkts_context_v1.so",
)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def junction(link, target):
    # Environment values remain data; they are never interpolated as shell code.
    env = dict(os.environ, ARKTS_JUNCTION_PATH=str(link), ARKTS_JUNCTION_TARGET=str(target))
    command = ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
               "$ErrorActionPreference='Stop'; New-Item -ItemType Junction "
               "-Path $env:ARKTS_JUNCTION_PATH -Target $env:ARKTS_JUNCTION_TARGET | Out-Null"]
    subprocess.run(command, env=env, check=True, capture_output=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", required=True,
                        choices=("x86_64-linux-ohos", "aarch64-linux-ohos"))
    parser.add_argument("--sdk-root", required=True, type=Path)
    parser.add_argument("--native-root", required=True, type=Path)
    parser.add_argument("--toolchain-root", type=Path,
                        help="complete CANGJIE_HOME; defaults to SDK build-tools")
    parser.add_argument("--cjpm", type=Path, help="defaults to toolchain tools/bin/cjpm.exe")
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("this SDK build driver currently supports the Windows OHOS SDK")
    sdk, native = args.sdk_root.resolve(), args.native_root.resolve()
    toolchain = (args.toolchain_root or sdk / "build-tools").resolve()
    cjpm = (args.cjpm or toolchain / "tools/bin/cjpm.exe").resolve()
    work, output = args.work_dir.resolve(), args.output_dir.resolve()
    source = Path(__file__).resolve().with_name("sdk")
    repo = source.parents[2]
    for path in (work, output):
        if path.exists() or path.is_relative_to(repo):
            parser.error("work/output must be fresh directories outside the compiler repository")
        # CJPM 1.2 builds cmd.exe command strings with unquoted project paths.
        if any(c.isspace() for c in str(path)) or any(c in str(path) for c in '&|<>^%"'):
            parser.error("work/output paths must contain no whitespace or cmd.exe metacharacters")
    if work.is_relative_to(output) or output.is_relative_to(work):
        parser.error("work and output directories must not contain each other")
    arch = args.target.split("-", 1)[0]
    api = sdk / "api/lib" / f"linux_ohos_{arch}_cjnative"
    cjc = toolchain / "bin/cjc.exe"
    readelf = native / "llvm/bin/llvm-readelf.exe"
    required = (cjpm, cjc, cjc.with_name("cjc-frontend.exe"), readelf,
                api / "ohos/ohos.ark_interop.cjo", api / "ohos/libohos.ark_interop.so",
                toolchain / "modules" / f"linux_ohos_{arch}_cjnative/std.cjo")
    for path in required:
        if not path.is_file():
            parser.error(f"missing required input: {path}")
    clang_paths = sorted((native / "llvm/lib/clang").glob(f"*/lib/{args.target}"))
    if len(clang_paths) != 1:
        parser.error("expected exactly one target Clang runtime directory")
    sources = sorted(p for p in source.rglob("*") if p.suffix in (".cj", ".toml"))
    source_hashes = {p.relative_to(source).as_posix(): sha256(p) for p in sources}
    inputs = set(required)
    inputs.add(toolchain / "third_party/llvm/bin/ld.lld.exe")
    # Freeze metadata, linker scripts, and the target's available link inputs.
    for directory in (api / "ohos", toolchain / "modules" / f"linux_ohos_{arch}_cjnative",
                      toolchain / "lib" / f"linux_ohos_{arch}_cjnative",
                      toolchain / "runtime/lib" / f"linux_ohos_{arch}_cjnative",
                      native / f"sysroot/usr/lib/{args.target}",
                      native / f"llvm/lib/{args.target}", clang_paths[0]):
        inputs.update(p for p in directory.rglob("*") if p.is_file() and (
            p.suffix in (".cjo", ".bc", ".a", ".o", ".lds") or ".so" in p.name))
    input_hashes = {str(p): sha256(p) for p in sorted(inputs)}
    work.mkdir(parents=True)
    output.mkdir(parents=True)
    shutil.copytree(source, work / "project", ignore=shutil.ignore_patterns("target", "cjpm.lock"))
    for name, target in (("sdk", sdk), ("native", native), ("toolchain", toolchain)):
        junction(work / name, target)
    project = work / "project"
    native_view, toolchain_view = work / "native", work / "toolchain"
    api_view = work / "sdk/api/lib" / f"linux_ohos_{arch}_cjnative"
    options = ["--dy-std", "--diagnostic-format=noColor", "-B",
               str(native_view / f"sysroot/usr/lib/{args.target}"), "--sysroot", str(native_view / "sysroot")]
    for directory in (native_view / f"sysroot/usr/lib/{args.target}",
                      native_view / clang_paths[0].relative_to(native),
                      native_view / f"llvm/lib/{args.target}"):
        options.extend(["-L", str(directory)])
    with (project / "cjpm.toml").open("a", encoding="utf-8") as config:
        config.write("compile-option = " + json.dumps(subprocess.list2cmdline(options)) + "\n")
    for member in ("context", "arkts"):
        with (project / member / "cjpm.toml").open("a", encoding="utf-8") as config:
            config.write(f"\n[target.{args.target}.bin-dependencies]\npath-option = " +
                         json.dumps([str(api_view / "ohos")]) + "\n")
    project_hashes = {p.relative_to(project).as_posix(): sha256(p)
                      for p in project.rglob("*") if p.suffix in (".cj", ".toml")}
    env = dict(os.environ, CANGJIE_HOME=str(toolchain_view))
    env["PATH"] = os.pathsep.join([str(toolchain_view / "bin"), str(native_view / "llvm/bin"),
                                   str(toolchain_view / "third_party/llvm/bin"), env.get("PATH", "")])
    for name, command in (("cjpm-version", [str(cjpm), "--version"]), ("cjc-version", [str(cjc), "-v"])):
        result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)
        (output / (name + ".txt")).write_bytes(result.stdout)
    command = [str(cjpm), "build", "--target", args.target, "--target-dir", str(work / "target"),
               "--jobs", "1", "--verbose"]
    manifest = {"schema_version": 1, "sdk_package": "arkts.lang", "sdk_version": "1.0.0",
                "target": args.target, "context_abi": 1, "logical_package": "cj_arkts_context_v1",
                "provider_filename": "libcj_arkts_context_v1.so", "source_files": source_hashes,
                "configured_project_files": project_hashes,
                "build_driver_sha256": sha256(Path(__file__)), "compiler_inputs": input_hashes,
                "build_command": command, "build_cwd": str(project),
                "build_environment": {"CANGJIE_HOME": env["CANGJIE_HOME"], "PATH": env["PATH"]},
                "consumer_link_mode": "one_dynamic_provider_per_application",
                "import_root": "modules", "package_dirs": ["modules/arkts", "modules/cj_arkts_context_v1"],
                "shared_libraries": ["modules/" + p for p in PRODUCTS if p.endswith(".so")],
                "build_status": "BuildPending", "runtime_verified": False}
    manifest_path = output / "arkts-sdk-manifest.json"
    write_json(manifest_path, manifest)
    result = subprocess.run(command, cwd=project, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (output / "cjpm-build.log").write_bytes(result.stdout)
    manifest.update(build_exit_code=result.returncode, build_status="BuildFailed")
    write_json(manifest_path, manifest)
    if result.returncode:
        raise SystemExit(result.returncode)
    release = work / "target" / args.target / "release"
    actual = {p.relative_to(release).as_posix() for p in release.rglob("*") if p.suffix in (".cjo", ".so")}
    if actual != set(PRODUCTS):
        raise RuntimeError(f"SDK product set mismatch: {sorted(actual)}")
    if any(sha256(Path(p)) != h for p, h in input_hashes.items()) or any(
            sha256(source / p) != h for p, h in source_hashes.items()) or any(
            sha256(project / p) != h for p, h in project_hashes.items()) or (
            sha256(Path(__file__)) != manifest["build_driver_sha256"]):
        raise RuntimeError("SDK build inputs changed during compilation")
    inspections = {}
    for relative in PRODUCTS:
        product = release / relative
        destination = output / "modules" / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(product, destination)
        if product.suffix == ".so":
            inspect = subprocess.run([str(readelf), "-d", "-s", "-W", str(product)],
                                     capture_output=True, check=True)
            (output / (product.name + ".elf.txt")).write_bytes(inspect.stdout)
            text = inspect.stdout.decode("utf-8", errors="replace")
            if "__CJGlobalInitFunc" not in text or "INIT_ARRAY" not in text:
                raise RuntimeError(f"missing ordinary package initialization: {relative}")
            inspections[product.name] = {"ordinary_initialization": True,
                                        "context_needed": "Shared library: [libcj_arkts_context_v1.so]" in text}
    if not inspections["libarkts.lang.so"]["context_needed"]:
        raise RuntimeError("arkts.lang is missing its sole dynamic context dependency")
    if "Shared library: [libohos.ark_interop.so]" not in (output / "libcj_arkts_context_v1.so.elf.txt").read_text():
        raise RuntimeError("context provider is missing the existing interop dependency")
    manifest["products"] = [{"file": "modules/" + p, "sha256": sha256(output / "modules" / p),
                             "bytes": (output / "modules" / p).stat().st_size} for p in PRODUCTS]
    manifest.update(build_status="Compiled", elf_inspections=inspections)
    write_json(manifest_path, manifest)
    print(manifest_path)


if __name__ == "__main__":
    main()
