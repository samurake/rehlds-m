# Test-only frozen baseline, deliberately without GLIBC_2.35 backports.
# Never ship this old OS or its libc in a release archive.
FROM almalinux:9.4@sha256:1c718a4cd7bab3bdb069ddbbd1eb593a390e6932d51d0048a2f6556303bafba7
RUN dnf --disablerepo='*' \
    --repofrompath=baseline,https://repo.almalinux.org/vault/9.4/BaseOS/x86_64/os/ \
    --setopt=baseline.gpgcheck=1 \
    --setopt=baseline.gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-AlmaLinux-9 \
    install -y glibc.i686 binutils python3 \
    && dnf clean all
WORKDIR /candidate
