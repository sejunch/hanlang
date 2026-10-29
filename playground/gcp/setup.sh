#!/bin/bash
# 한랭 놀이터 — Google Cloud e2-micro (Debian 12) 에 설치. VM 의 SSH 창에서:
#   curl -fsSL https://raw.githubusercontent.com/sejunch/hanlang/main/playground/gcp/setup.sh | sudo bash
# 다시 돌려도 된다 (최신 코드로 다시 빌드하고 서비스를 다시 띄움).
#
# 하는 일
#   - gcc·X11·한글 글꼴·Caddy 설치, 저장소를 /opt/hanlang 에 받아 빌드
#   - 메모리가 1GB 라 스왑 1GB
#   - 놀이터 서버를 전용 사용자(hanlang)로, 밖으로 인터넷을 못 쓰게 막은 systemd 서비스로
#   - Caddy 가 <외부 IP>.sslip.io 이름으로 무료 HTTPS 인증서를 받아 서버 앞에 섬
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "sudo 로 돌려야 함"; exit 1; }

echo "== 패키지"
export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q gcc libc6-dev make python3 git ca-certificates curl \
    xvfb libx11-dev libxft-dev fonts-noto-cjk
if ! apt-get install -y -q caddy; then          # 데비안 저장소에 없으면 Caddy 공식 저장소에서
    apt-get install -y -q debian-keyring debian-archive-keyring apt-transport-https gnupg
    curl -1sLf https://dl.cloudsmith.io/public/caddy/stable/gpg.key | gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
    curl -1sLf https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt > /etc/apt/sources.list.d/caddy-stable.list
    apt-get update -q && apt-get install -y -q caddy
fi

echo "== 스왑 1GB"
if ! swapon --show | grep -q /swapfile; then
    fallocate -l 1G /swapfile && chmod 600 /swapfile && mkswap /swapfile && swapon /swapfile
    grep -q /swapfile /etc/fstab || echo '/swapfile none swap sw 0 0' >> /etc/fstab
fi

echo "== 사용자·코드"
id hanlang >/dev/null 2>&1 || useradd --system --create-home --home-dir /var/lib/hanlang --shell /usr/sbin/nologin hanlang
if [ -d /opt/hanlang/.git ]; then
    git -C /opt/hanlang fetch -q origin main && git -C /opt/hanlang reset -q --hard origin/main
else
    git clone -q --depth 1 https://github.com/sejunch/hanlang.git /opt/hanlang
fi
make -C /opt/hanlang hanc runtime/runtime.s runtime/runtime-window.s
chown -R root:root /opt/hanlang            # 서버 사용자는 코드를 못 고침 (읽기만)

echo "== 서비스"
cat > /etc/systemd/system/hanlang-playground.service <<'UNIT'
[Unit]
Description=한랭 놀이터
After=network.target

[Service]
User=hanlang
Group=hanlang
WorkingDirectory=/opt/hanlang
Environment=HOST=127.0.0.1 PORT=7860 HAN_JOBS=1 HAN_MEM_MB=384
ExecStart=/usr/bin/python3 /opt/hanlang/playground/server.py
Restart=always
RestartSec=2
# 남의 코드가 돌아가는 서비스라 묶어 둔다
NoNewPrivileges=yes
PrivateTmp=yes
PrivateDevices=yes
ProtectSystem=strict
ProtectHome=yes
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectControlGroups=yes
RestrictSUIDSGID=yes
ReadWritePaths=/var/lib/hanlang
IPAddressAllow=localhost
IPAddressDeny=any
MemoryMax=700M
TasksMax=64

[Install]
WantedBy=multi-user.target
UNIT
systemctl daemon-reload
systemctl enable -q hanlang-playground
systemctl restart hanlang-playground

echo "== HTTPS (Caddy)"
OUTIP=$(curl -fs -H "Metadata-Flavor: Google" \
    http://metadata.google.internal/computeMetadata/v1/instance/network-interfaces/0/access-configs/0/external-ip || true)
[ -n "$OUTIP" ] || OUTIP=$(curl -fs https://api.ipify.org || true)
SITE="${OUTIP//./-}.sslip.io"
cat > /etc/caddy/Caddyfile <<CADDY
$SITE {
    encode gzip
    reverse_proxy 127.0.0.1:7860
}
CADDY
systemctl enable -q caddy
systemctl restart caddy

sleep 2
if curl -fs http://127.0.0.1:7860/ >/dev/null; then echo "서버: 돌고 있음"; else echo "서버: 안 뜸 — journalctl -u hanlang-playground 로 확인"; fi
echo
echo "놀이터 주소: https://$SITE"
echo "(인증서를 처음 받는 데 1분쯤 걸릴 수 있음. 안 열리면 VM 설정에서 HTTP·HTTPS 트래픽 허용을 켰는지 확인)"
