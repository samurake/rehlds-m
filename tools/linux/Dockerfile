# Use the maintained EL8 unwinder ABI, which does not import GLIBC_2.35.
# The EL9 GCC runtime backports _dl_find_object and raises the deployment floor.
FROM almalinux:8@sha256:9f355ae942d6a6c0561f0771dc053a2cfae9580fc45fa4252756db7c7e80c09f AS gcc-runtime
RUN dnf install -y libgcc.i686 \
    && mkdir -p /runtime/licenses \
    && cp -L /usr/lib/libgcc_s.so.1 /runtime/libgcc_s.so.1 \
    && cp /usr/share/licenses/libgcc/* /runtime/licenses/ \
    && rpm -q --qf '%{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}\nSource: %{SOURCERPM}\n' libgcc.i686 > /runtime/package.txt \
    && dnf clean all

FROM almalinux:9@sha256:3a3fa7f043b142bc8008c8b308d39b47d2c84008addcd52f9f9a7a82d2a90474

RUN dnf install -y gcc-c++ glibc-devel.i686 libstdc++-devel.x86_64 libstdc++-devel.i686 \
       cmake make git binutils file python3 libasan.i686 libubsan.i686 \
    && dnf clean all

COPY --from=gcc-runtime /runtime /opt/rehldsm-runtime
WORKDIR /work
