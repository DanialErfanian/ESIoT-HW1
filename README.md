# Embedded/IoT Security - Homework 1

## Student Info

- Name: `Danial Erfanian`
- Student ID: `402211745`

---

## Execution Specifications

* Board: `ESP32-S3`
* Device model: `ce-40876`
* Laptop IP: `10.114.151.190`
* HTTP port: `8000`
* Serial port: `/dev/ttyACM0`

Flash detection output:

```text
Chip type: ESP32-S3
Detected flash size: 16MB
MAC: 1c:db:d4:99:55:e8
````

---

## Firmware File Preparation

```bash
mkdir -p files
cp E1_Students/build/CE40876_E2_update.bin files/
```

---

## Creating the Manifest

```bash
python3 tools/make_manifest.py \
  --firmware files/CE40876_E2_update.bin \
  --version 2 \
  --base-url http://10.114.151.190:8000 \
  --model ce-40876 \
  --out files/manifest.json
```

## Manifest:

```json
{
  "device_model": "ce-40876",
  "version": 2,
  "size": 901472,
  "sha256": "ab8bdc79a6ede66cb6e5522fc1fe67a6aa5b54b8fdb8911cb978bd41d8ec49ef",
  "firmware_url": "http://10.114.151.190:8000/CE40876_E2_update.bin"
}
```

---

## Running the HTTP Server

```bash
cd files
python3 -m http.server 8000
```

Manifest URL used:

```text
http://10.114.151.190:8000/manifest.json
```

---

## Available Commands on the Board

According to the current firmware, the supported commands are:

```text
help
version
update_apply <manifest_url>
reset_state CONFIRM
```

---

## Applying the Update on the Board

In the serial monitor, with baud rate `115200`:

```text
update_apply http://10.114.151.190:8000/manifest.json
```

Output before the update:

```text
> version
device_model=ce-40876
current_version=1
update_state=ready
```

Output during a successful update:

```text
> update_apply http://10.114.151.190:8000/manifest.json
UPDATE_OK version=2
```

Output after the successful update:

```text
> version
device_model=ce-40876
current_version=2
update_state=ready
```

---

## Checks That the Current Firmware Actually Performs

In the current version of `main.c`, the `cmd_update_apply` function performs the following steps in order:

1. Downloads the manifest over HTTP
2. Parses the manifest fields
3. Checks whether `device_model` matches `ce-40876`
4. Downloads the firmware file chunk by chunk while computing the streaming `SHA-256` and the actual size
5. Checks whether `size` matches
6. Checks whether `sha256` matches
7. Stores `version` in NVS and prints `UPDATE_OK version=N`

Important note: the firmware file is not written to any flash partition and is not executed. It is only downloaded and hashed for validation. The only value that is persistently changed is the `version` stored in NVS.

---

## Outputs

Errors:

```text
ERR usage: update_apply <manifest_url>
ERR manifest_download <esp_err>
ERR manifest_parse
ERR device_model_mismatch
ERR firmware_download <esp_err>
ERR size_mismatch expected=<n> actual=<m>
ERR sha256_mismatch
```

---

## Actual Behavior in Test Scenarios

| Scenario                                    | Current firmware behavior                              |
| ------------------------------------------- | ------------------------------------------------------ |
| Valid package with a newer version          | Accepted → `UPDATE_OK version=N`                       |
| Binary file changes but the hash is correct | Accepted, because the new hash matches the manifest    |
| Manifest with an invalid signature          | **Not checked** and accepted — vulnerability           |
| Version older than the stored version       | **Not rejected** and accepted — rollback vulnerability |
| Wrong device model                          | Rejected → `ERR device_model_mismatch`                 |
| Incorrect file hash                         | Rejected → `ERR sha256_mismatch`                       |
| Incorrect file size                         | Rejected → `ERR size_mismatch ...`                     |

---

## Security Analysis

The `SHA-256` check only shows that the downloaded file matches the hash value inside the manifest. However, if an attacker can modify both the binary file and the manifest, they can also place the new hash inside the manifest, and the device will consider the package valid.

Therefore, a hash alone is not enough for trust. To trust the package, the manifest itself must have a digital signature, because important fields such as `version`, `device_model`, `size`, `sha256`, and `firmware_url` are all inside the manifest.

In addition, because the current version does not perform any check on version ordering, a rollback attack is possible: an attacker can reinstall an older, and possibly vulnerable, version.

---

## Testing a Tampered Package

The goal of this test is to answer the following question: if an attacker can modify **both the binary file and the manifest together**, will the device still accept the package?

### 1) Creating a Tampered Binary

We create a fake firmware file whose contents differ from the original binary:

```bash
cp E1_Students/build/CE40876_E2_update.bin files/CE40876_E2_update.bin
echo 'MALICIOUS_PAYLOAD' >> files/CE40876_E2_update.bin
```

This changes both the file contents and its `size` compared to the original version, so the `sha256` will also be completely different.

Below is the output of changing the binary file without changing the hash and size. In the second attempt, the size was corrected, but the hash mismatch caused an error:

```shell
> version
device_model=ce-40876
current_version=1
update_state=ready
>     
> update_apply http://10.114.151.190:8000/manifest.json
UPDATE_OK version=2
> update_apply http://10.114.151.190:8000/manifest.json
ERR size_mismatch expected=901490 actual=901509
> update_apply http://10.114.151.190:8000/manifest.json
ERR sha256_mismatch
> 
```

### 2) Creating a Manifest Corresponding to the Tampered Binary

Since `make_manifest.py` calculates the `size` and `sha256` from the actual file, the new manifest will be **consistent** with the tampered binary:

```bash
python3 tools/make_manifest.py \
  --firmware files/CE40876_E2_update.bin \
  --version 3 \
  --base-url http://10.114.151.190:8000 \
  --model ce-40876 \
  --out files/manifest.json
```

Generated manifest, where `size` and `sha256` match the tampered file:

```json5
{
  "device_model": "ce-40876",
  "version": 3,
  "size": 901509,
  "sha256": "e34a31871c21d99c1656d92358775d465670d798bad5306a2a0e006a0f126c13",
  "firmware_url": "http://10.114.151.190:8000/CE40876_E2_update.bin" // MALICIOUS CODE INCLUDED
}
```

Here, the attacker has not only changed the binary, but also updated the `size` and `sha256` fields inside the manifest to the new values of that same binary. In other words, the manifest and the binary are consistent with each other.

### 3) Running on the Board and Documenting the Result

```text
> update_apply http://10.114.151.190:8000/manifest.json
UPDATE_OK version=3
```

Checking the version afterward:

```text
> version
device_model=ce-40876
current_version=3
update_state=ready
```

**Result:** the device **accepts** the tampered package. This is because all checks performed by the current firmware — `device_model`, `size`, and `sha256` — pass successfully. The `size` and `sha256` values inside the manifest exactly match the tampered binary file, and the `signature` field is ignored.

### 4) Why Does This Happen?

The `SHA-256` check only verifies the match between the “downloaded file” and the “hash value written in the manifest.” When the attacker controls both, they can create a consistent pair — a binary containing malicious code plus a manifest containing that binary’s hash and size — and the hash check will still pass.

---

## What Exactly Does SHA-256 Guarantee, and What Does It Not Guarantee?

**What SHA-256 guarantees — integrity relative to the manifest:**

* Integrity of the file relative to the hash value inside the manifest; that is, the file was not accidentally corrupted, truncated, or changed during transfer or on the server.
* If the binary file changes but the `sha256` value in the manifest remains fixed, the check **fails**, as in the `ERR sha256_mismatch` case.
* Detection of unintentional corruption, such as network noise, incomplete files, or copy errors.

**What SHA-256 does not guarantee — authenticity / trust:**

* It does not guarantee authenticity or trustworthiness of the source; the hash does not say who created the file.
* If an attacker controls both the binary and the manifest, they can write the new hash into the manifest, making the hash check ineffective, as shown in the test above.
* It does not prevent rollback attacks; an old version can also have a correct hash.

In short: **SHA-256 provides integrity, but not authenticity.** For authenticity, the manifest must be **digitally signed** with a private key, for example using ECDSA P-256 over the canonical form of the fields, and the device must verify the signature using the trusted public key. In that case, an attacker who does not have the private key cannot create a consistent and valid signed manifest, even if they modify both the binary and the manifest.

---

## Rollback to an Older Version

In this test, it was shown that the current firmware does not perform any check to prevent installation of an older version. Although the stored version on the device was 2, the manifest for version 1 was accepted, and the version value in NVS was changed to 1. This behavior indicates a rollback vulnerability: an attacker can revert the device to an older and possibly vulnerable version. To prevent this issue, the firmware must only accept versions whose `version` value is greater than the current version.

```shell
> version
device_model=ce-40876
current_version=2
update_state=ready
>     
> update_apply http://10.114.151.190:8000/manifest.json
UPDATE_OK version=1
```

---

## Vulnerabilities Identified in the Current Version

* The digital signature of the manifest is **not validated**; the `signature` field is only parsed and ignored.
* There is no check to prevent rollback; any version, even an older one, is accepted.

---

## Proposed Security Fixes

To secure this service, the following measures are recommended:

* Validate the manifest’s digital signature, using ECDSA P-256 over the canonical form of the fields, and reject manifests with invalid signatures.
* Check the version and reject packages whose version is equal to or older than the stored version, in order to prevent rollback.
* Use HTTPS to download the manifest and firmware. Although a digital signature on the manifest and validation during the update reduce concerns about HTTP attacks, HTTPS may still be needed for confidentiality.

> Creating a signed manifest with the existing script is possible:
>
> ```bash
> python3 tools/make_manifest.py \
>   --firmware files/CE40876_E2_update.bin \
>   --version 2 \
>   --base-url http://10.114.151.190:8000 \
>   --model ce-40876 \
>   --key private_key.pem \
>   --out files/manifest.json
> ```
>
> To create a bad signature for testing, the `--bad-signature` flag can be used.

---

## Step 4: Design and Implementation Fixes

To fix the observed security weaknesses, the update design must be changed so that the firmware first verifies the authenticity of the manifest before accepting any package, and then prevents installation of older versions. In this step, two main fixes are implemented:

1. Validation of the manifest’s digital signature
2. Prevention of rollback by checking the version number

First, signing the manifest requires a private/public key pair. The private key is kept only on the manifest producer’s side and is used to sign the manifest. The public key is embedded in the firmware so that the device can verify the signature.

The keys were generated using the following commands:

```shell
# Private key (keep secret, used by the signer)
openssl ecparam -name prime256v1 -genkey -noout -out priv.pem

# Public key (PEM, just for reference)
openssl ec -in priv.pem -pubout -out pub.pem
```

After generating the keys, the `make_manifest.py` file was updated so that it can sign the manifest using the private key. As a result, important manifest fields such as `device_model`, `version`, `size`, `sha256`, and `firmware_url` can no longer be modified by an attacker, because any change to these fields invalidates the signature.

For the firmware to verify the signature, the public key must be placed inside `main.c`. The following code was used to extract the public key as a C array:

```python
def print_pubkey_c_array(key):
    pub = key.public_key().public_bytes(
        serialization.Encoding.X962,
        serialization.PublicFormat.UncompressedPoint,
    )
    assert len(pub) == 65 and pub[0] == 0x04
    print('static const uint8_t g_manifest_pubkey[65] = {')
    for i in range(0, 65, 8):
        chunk = pub[i:i + 8]
        print('    ' + ' '.join(f'0x{b:02x},' for b in chunk))
    print('};')
```

The output of this function is placed in `main.c` as a 65-byte array. This array is the P-256 curve public key and is used by the signature verification function.

In the firmware, the `verify_manifest_signature` function was also added. This function verifies the signature inside the manifest using the public key. The important point is that signature verification must happen before trusting any manifest field, because until the signature is valid, values such as version, hash, size, and firmware URL cannot be trusted.

The main update logic in `main.c` was modified as follows:

```c++
    /* 1) Authenticate the WHOLE manifest first. Once the signature is valid,
     *    every field (model, version, size, hash, url) can be trusted. */
    if (!verify_manifest_signature(&m)) {
        uart_write_str("ERR signature_invalid\r\n");
        return;
    }

    /* 2) Device model must match this device. */
    if (strcmp(m.device_model, DEVICE_MODEL) != 0) {
        uart_write_str("ERR device_model_mismatch\r\n");
        return;
    }

    /* 3) Anti-rollback: reject versions lower than the last valid version. */
    if (m.version <= g_current_version) {
        uart_printf("ERR version_too_old offered=%lu current=%lu\r\n",
                    (unsigned long)m.version, (unsigned long)g_current_version);
        return;
    }

    /* 4) Download the binary, computing size and SHA-256 on the fly. */
    uint8_t actual_hash[32];
    uint32_t actual_size = 0;
    err = http_hash_and_size(m.firmware_url, actual_hash, &actual_size);
    if (err != ESP_OK) {
        uart_printf("ERR firmware_download %s\r\n", esp_err_to_name(err));
        return;
    }

    char actual_hex[65];
    bytes_to_hex(actual_hash, sizeof(actual_hash), actual_hex, sizeof(actual_hex));

    /* 5) Size must match the signed manifest. */
    if (actual_size != m.size) {
        uart_printf("ERR size_mismatch expected=%lu actual=%lu\r\n",
                    (unsigned long)m.size, (unsigned long)actual_size);
        return;
    }

    /* 6) SHA-256 must match the signed manifest. */
    if (strcasecmp(actual_hex, m.sha256) != 0) {
        uart_write_str("ERR sha256_mismatch\r\n");
        return;
    }

    /* 7) All checks passed: store the new last-valid version in NVS. */
    g_current_version = m.version;
    nvs_write_u32_val(KEY_VERSION, g_current_version);
    uart_printf("UPDATE_OK version=%lu\r\n", (unsigned long)g_current_version);
```

With these changes, the order of checks is more secure. First, the manifest is authenticated; then the device model and version are checked; finally, the firmware file is downloaded and its size and hash are compared with the signed values inside the manifest.

In the new design, if an attacker modifies both the binary and the manifest, they can no longer create a valid fake package, because producing a valid signature requires the private key. Also, if a manifest for an older version is provided, the firmware rejects it with `ERR version_too_old`. Therefore, the two main vulnerabilities in the previous version — lack of manifest authenticity verification and rollback possibility — are fixed in this step.

## Manifests:

### 1-manifest_valid.json

```json
{
  "device_model": "ce-40876",
  "version": 5,
  "size": 902256,
  "sha256": "71c1988630257f3191b986b12041c4e801220a16706cf8f873ad14a40e0c777f",
  "firmware_url": "http://10.114.151.190:8000/firmware_valid.bin",
  "signature": "f91ca992eff1e1610630465a42dda0f870a04311ddd36c69b3ddcc1ec38953a98bc449ece870937afc1a0a48cc932464b242bca21da4a8c31dcec6deb2c09f9d"
}
```

### 2-manifest_tampered.json

```json
{
  "device_model": "ce-40876",
  "version": 6,
  "size": 902256,
  "sha256": "71c1988630257f3191b986b12041c4e801220a16706cf8f873ad14a40e0c777f",
  "firmware_url": "http://10.114.151.190:8000/CE40876_E2_update.bin",
  "signature": "4b19e4f752b7923a54f684dbea04251f5fa9ddc487316a63463259d01e2ab73a5be7983eb1e79c6eec1c828e22d41621e242b4f9540ff9a9df4a574af70b1c7c"
}
```

### 3-manifest_old_version.json

```json
{
  "device_model": "ce-40876",
  "version": 1,
  "size": 902256,
  "sha256": "71c1988630257f3191b986b12041c4e801220a16706cf8f873ad14a40e0c777f",
  "firmware_url": "http://10.114.151.190:8000/firmware_valid.bin",
  "signature": "abdc33dd61a52fb873d14330de1d3a72ec9fc61ad8066a0adc116167fc004830c22608567983b9701eaae880de1675c01eb10edeb019aa0d43807dc73945f3a9"
}
```

### 4-manifest_wrong_model.json

```json
{
  "device_model": "EE-40876",
  "version": 5,
  "size": 902256,
  "sha256": "71c1988630257f3191b986b12041c4e801220a16706cf8f873ad14a40e0c777f",
  "firmware_url": "http://10.114.151.190:8000/firmware_valid.bin",
  "signature": "b78618f6494069c6c6914d893728c834bab4b4a4dc693298b307f7cbafe2550edb5b00dd9ca5067d6d66f698714d012fa4fa10865a946905385c66692ce2d7c6"
}
```

### 5-manifest_bad_hash.json

```json
{
  "device_model": "ce-40876",
  "version": 5,
  "size": 902256,
  "sha256": "11c1988630257f3191b986b12041c4e801220a16706cf8f873ad14a40e0c777f",
  "firmware_url": "http://10.114.151.190:8000/firmware_valid.bin",
  "signature": "57112116ccb53172eeacaab4fa0a09afee4b6eaa52aeb8ab4c31d3fcd8be0e6e9b1dcec79b0444c25cc0fab8625f5754878e6846dd06d99a40f142770a66abb8"
}
```

## Conclusion

In this exercise, it was shown that checking SHA-256 only guarantees the integrity of the firmware file relative to the value inside the manifest, and is not enough on its own to trust the package. In the initial version, because the manifest’s digital signature was not checked, an attacker could modify both the firmware file and the manifest, and the fake package would still be accepted. Also, because there was no version check, rollback to an older version was possible.

To fix these weaknesses, in the new design the digital signature of the manifest is first verified using the public key embedded in the firmware, and then the proposed version is compared with the version stored in NVS. This way, fake packages and older versions are rejected, and the update process becomes more secure.

## Limitations

* In this exercise, the firmware file is not written to or executed from an OTA partition.
* The focus is only on manifest validation, hash/size checking, version policy, and recording the version in NVS.
* HTTP is used, so confidentiality of the download path is not guaranteed; however, manifest authenticity is controlled with a digital signature.
