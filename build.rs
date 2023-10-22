use regex::Regex;
use std::{env, fs};
use std::path::PathBuf;
use std::process::Command;

macro_rules! exec {
    ($($tokens: tt)*) => {
        let out = $($tokens)*.output().unwrap();
        println!("cargo:warning=build-info\r\x1b[32;1m{}", String::from_utf8_lossy(&out.stdout).replace("\n","\r"))
    }
}

fn main() -> miette::Result<()> {
    let target = env::var("TARGET").unwrap();
    println!("cargo:warning=target:{}", target);

    let workspace = PathBuf::from(&env::var("CARGO_MANIFEST_DIR").unwrap());
    let src = workspace.join("src");
    let omnicore = workspace.join("omnicore").join("src");
    let cxx = PathBuf::from("/usr/bin/clang++");

    exec!(Command::new("git").args(["submodule", "update", "--init", "--recursive"]));
    exec!(Command::new("./autogen.sh").current_dir(&workspace.join("omnicore")));
    exec!(Command::new("./configure")
        .current_dir(&workspace.join("omnicore"))
        .args([
            "CXX=clang++",
            "CC=clang",
            "--disable-wallet",
            "--disable-zmq",
            "--disable-bench",
            "--disable-tests",
            "--disable-fuzz-binary",
            "--without-gui",
            "--without-miniupnpc",
            "--without-natpmp",
        ]));
    exec!(Command::new("make")
        .current_dir(&workspace.join("omnicore"))
        .arg("-j8"));

    // tell dependent crates where to find the native omnicore library
    let mut libs=vec![];
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    for entry in glob::glob("omnicore/**/*.a").unwrap() {
        let entry = entry.unwrap();
        fs::copy(entry.clone(), out_dir.join(entry.file_name().unwrap())).unwrap();
        if entry.to_string_lossy().ends_with("bitcoin_crypto_x86_shani.a") {
            libs = vec![
                "bitcoin_node",
                "bitcoin_common",
                "bitcoin_util",
                "univalue",
                "bitcoin_consensus",
                "bitcoin_crypto_base",
                "bitcoin_crypto_sse41",
                "bitcoin_crypto_avx2",
                "bitcoin_crypto_x86_shani",
                "leveldb",
                "crc32c",
                "crc32c_sse42",
                "memenv",
                "secp256k1",
                "m",
            ]
        }else if entry.to_string_lossy().ends_with("bitcoin_crypto_arm_shani.a") {
            libs = vec![
                "bitcoin_node",
                "bitcoin_common",
                "bitcoin_util",
                "univalue",
                "bitcoin_consensus",
                "bitcoin_crypto_base",
                "bitcoin_crypto_arm_shani",
                "leveldb",
                "crc32c",
                "crc32c_arm_crc",
                "memenv",
                "secp256k1",
                "m",
            ];
        }
    }
    println!("cargo:root={}", out_dir.to_string_lossy());
    println!("cargo:libs={}", libs.join(" "));

    let mut build = autocxx_build::Builder::new(
        &src.join("lib.rs"),
        &[
            &src,
            &omnicore,
            &omnicore.join("config"),
            &omnicore.join("leveldb").join("include"),
            &omnicore.join("univalue").join("include"),
            &omnicore.join("secp256k1").join("include"),
            &omnicore.join("crc32c").join("include"),
        ],
    )
    .build()?;

    build
        .compiler(cxx)
        .std("c++17")
        .define("HAVE_CONFIG_H", None)
        // .define("FETCH_REMOTE_TX", None)
        .file(&src.join("omni.cpp"))
        .compile("omni_ffi");

    println!(
        "cargo:rustc-link-search=native={}",
        omnicore.to_string_lossy()
    );
    println!(
        "cargo:rustc-link-search=native={}",
        omnicore.join(".libs").to_string_lossy()
    );
    println!(
        "cargo:rustc-link-search=native={}",
        omnicore.join("leveldb").join(".libs").to_string_lossy()
    );
    println!(
        "cargo:rustc-link-search=native={}",
        omnicore.join("crypto").join(".libs").to_string_lossy()
    );
    println!(
        "cargo:rustc-link-search=native={}",
        omnicore.join("crc32c").join(".libs").to_string_lossy()
    );
    println!(
        "cargo:rustc-link-search=native={}",
        omnicore.join("secp256k1").join(".libs").to_string_lossy()
    );

    println!("cargo:rustc-link-arg=-Wl,--start-group");
    println!("cargo:rustc-link-arg=-lbitcoin_node");
    println!("cargo:rustc-link-arg=-lbitcoin_consensus");
    println!("cargo:rustc-link-arg=-lbitcoin_util");
    println!("cargo:rustc-link-arg=-lbitcoin_common");
    println!("cargo:rustc-link-arg=-lbitcoin_crypto_base");
    println!("cargo:rustc-link-arg=-lleveldb");
    println!("cargo:rustc-link-arg=-lmemenv");
    println!("cargo:rustc-link-arg=-lcrc32c");
    println!("cargo:rustc-link-arg=-lsecp256k1");
    println!("cargo:rustc-link-arg=-lunivalue");
    if Regex::new(r"arm|aarch64").unwrap().is_match(&target) {
        println!("cargo:rustc-link-arg=-lbitcoin_crypto_arm_shani");
        println!("cargo:rustc-link-arg=-lcrc32c_arm_crc");
    } else if Regex::new(r"x86_64").unwrap().is_match(&target) {
        println!("cargo:rustc-link-arg=-lbitcoin_crypto_x86_shani");
        println!("cargo:rustc-link-arg=-lbitcoin_crypto_avx2");
        println!("cargo:rustc-link-arg=-lbitcoin_crypto_sse41");
        println!("cargo:rustc-link-arg=-lcrc32c_sse42");
    }
    println!("cargo:rustc-link-arg=-Wl,--end-group");
    println!("cargo:rustc-link-arg=-lcpp-httplib");
    println!("cargo:rustc-link-arg=-lm");

    println!("cargo:rerun-if-changed=src/lib.rs");
    Ok(())
}
