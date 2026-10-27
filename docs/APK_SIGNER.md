# Agent-EJ Native APK Signer

## Introduction
Agent-EJ includes an ultra-fast, zero-dependency, native C11 implementation of Android's APK Signature Schemes (v1, v2, and v3). This means you do not need Java, `apksigner`, `jarsigner`, or `OpenSSL` installed on the host system to sign or resign APKs.

## Basic Usage
### Default Signing
If no custom key is provided, Agent-EJ signs the APK using its built-in static test key (`CN=GrandpaEJ`).
```bash
./agent-ej tool resign_apk path=app.apk
```
This will generate `app.apk.signed` in the same directory using the default `v1` scheme.

### Selecting Signature Schemes
You can specify which signature schemes to use by passing the `scheme` parameter. Multiple schemes can be chained together:
```bash
# Sign using only V2
./agent-ej tool resign_apk path=app.apk scheme=v2

# Sign using V1, V2, and V3 (Recommended for maximum compatibility)
./agent-ej tool resign_apk path=app.apk scheme=v1,v2,v3
```

## Custom Keys (Dynamic Signing)
Agent-EJ allows you to dynamically load your own private key and certificate without recompiling the binary.
*(Note: Custom keys currently only support **V2 and V3** schemes. If you provide a custom key, V1 will be disabled automatically because dynamically generating a full PKCS#7 block in pure C requires a complex ASN.1 compiler).*

### 1. Prepare your keys
You need your Private Key in `PEM` format and your X.509 Certificate in `DER` format.

```bash
# Generate a new 2048-bit RSA key
openssl genrsa -out mykey.pem 2048

# Generate a certificate
openssl req -new -x509 -key mykey.pem -out mycert.pem -days 10000 -subj "/C=US/O=MyOrg/CN=MyName"

# Convert the PEM certificate to raw DER format
openssl x509 -in mycert.pem -out mycert.der -outform DER
```

### 2. Sign the APK
Pass the paths to your custom key and certificate:
```bash
./agent-ej tool resign_apk path=app.apk scheme=v2,v3 key=mykey.pem cert=mycert.der
```

---

## Advanced Architecture & Implementation Details

### Zero-Dependency Design
To adhere to the Agent-EJ modular architecture, the signer does **not** link against OpenSSL, BouncyCastle, or any external ASN.1 parsing libraries.
- **Hashes**: Uses native, memory-optimized SHA-1 and SHA-256 implementations.
- **RSA**: Uses a lightweight native RSA signing implementation.
- **Memory Optimization**: Uses POSIX `mmap` for reading the APK chunks. This bypasses heap allocations and ensures the RAM footprint remains under 500 KB even when hashing a 2GB APK!

### Scheme V1 (JAR Signing)
V1 signing modifies the ZIP structure by adding a `META-INF` directory containing:
1. `MANIFEST.MF` - Contains SHA-256 hashes of every file in the ZIP.
2. `CERT.SF` - Contains the hash of the Manifest itself, plus hashes of individual Manifest entries.
3. `CERT.RSA` (PKCS#7) - Contains the RSA signature of the `CERT.SF` file and the public certificate.

**Note on V1 Custom Keys:** Generating a completely dynamic `PKCS#7 SignedData` block requires an ASN.1 Builder to accurately encode the user's `IssuerAndSerialNumber`. Since modern Android versions (7.0+) natively rely on V2/V3 anyway, custom key support for V1 is skipped to keep the binary small.

### Schemes V2 and V3 (APK Signature Block)
Unlike V1, V2 and V3 do not modify the ZIP entries. Instead, they insert an opaque binary block (the **APK Signing Block**) right before the ZIP Central Directory.

Agent-EJ builds this block manually through memory chunking:
1. **Chunk Hashing**: The APK is divided into 1MB chunks. Each chunk is prepended with `0xa5` and hashed via SHA-256.
2. **Top-Level Hash**: All chunk hashes are combined, prepended with `0x5a`, and hashed again to form the final APK digest.
3. **Dynamic Public Key Extraction**: When a custom `.der` certificate is loaded, Agent-EJ natively scans the raw bytes to find the `rsaEncryption` OID (`1.2.840.113549.1.1.1`). It then backtracks to extract the exact `SubjectPublicKeyInfo` sequence required for the V2/V3 payload without needing a full X.509 parser! This avoids megabytes of library bloat.
4. **V3 Min/Max SDK**: The V3 signer block natively hardcodes `minSdk=24` and `maxSdk=0x7FFFFFFF`, maintaining 100% byte-compatibility with Google's official `apksigner`.

### File Structure Map
- `src/android/apk/format_apk_sign.c`: Orchestrator for V1 JAR Signing.
- `src/android/apk/format_apk_v2.c`: Dedicated block generator for Scheme V2.
- `src/android/apk/format_apk_v3.c`: Dedicated block generator for Scheme V3.
- `src/android/apk/format_apk_sign_block.c`: Orchestrator for V2/V3. Computes chunk hashes, extracts dynamic keys, and constructs the final aligned APK Signing Block.
