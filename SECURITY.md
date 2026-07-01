# Security Policy

This repository is intended for local-device provisioning and should not ship
with personal secrets or private infrastructure details in source control.

## Supported guidance

- Do not commit AP passwords, Wi-Fi credentials, certificates, or tokens.
- Saved Wi-Fi credentials are stored in flash/NVS/settings and are not encrypted
  unless platform security is enabled.
- Enable platform protections such as flash encryption and secure boot when the
  target device and deployment require stronger protection.

## Reporting a problem

If you find a security issue in this repository, report it privately through the
channel you use to reach the repository maintainer. Include the file path and a
short explanation of the exposure or misuse path.
