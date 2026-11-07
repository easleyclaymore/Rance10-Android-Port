#!/bin/sh
# Run INSIDE the Debian chroot as root, after chrootctl.sh has mounted the
# container. Exposes the chroot sshd on 8023 so all work can be done with:
#   ssh -p 8023 alin@192.168.1.136
# The 8023 port config lives in /etc/ssh/sshd_config.d/10-rance10.conf.
set -e
mkdir -p /run/sshd
chmod 0755 /run/sshd
/usr/sbin/sshd -t
if ! ss -ltn 2>/dev/null | grep -q ':8023'; then
    /usr/sbin/sshd
fi
ss -ltn 2>/dev/null | grep ':8023' || echo "sshd on 8023 not listening"
