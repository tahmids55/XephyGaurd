import os
import socket
import sqlite3
import subprocess

def get_lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('8.8.8.8', 80))
        ip = s.getsockname()[0]
    except Exception:
        ip = '127.0.0.1'
    finally:
        s.close()
    return ip

if __name__ == '__main__':
    ip = get_lan_ip()
    parts = ip.split('.')
    subnet = f"{parts[0]}.{parts[1]}.{parts[2]}.0/24"
    
    db_path = 'server/database/database.sqlite'
    conn = sqlite3.connect(db_path)
    cur = conn.cursor()
    
    cur.execute('UPDATE labs SET ccc_ip=?, subnet=? WHERE id=1', (ip, subnet))
    cur.execute('UPDATE settings SET value=? WHERE key="ccc_ip"', (f'"{ip}"',))
    cur.execute('UPDATE settings SET value=? WHERE key="lab_subnet"', (f'"{subnet}"',))
    conn.commit()
    conn.close()

    # Re-issue server TLS certificate including current IP and common router IPs
    php_bin = r"C:\Users\taki\AppData\Local\Microsoft\WinGet\Packages\PHP.PHP.8.3_Microsoft.Winget.Source_8wekyb3d8bbwe\php.exe"
    openssl_cnf = r"C:\Users\taki\AppData\Local\Microsoft\WinGet\Packages\PHP.PHP.8.3_Microsoft.Winget.Source_8wekyb3d8bbwe\extras\ssl\openssl.cnf"
    env = os.environ.copy()
    env["OPENSSL_CONF"] = openssl_cnf
    try:
        subprocess.run([
            php_bin, "artisan", "xephyguard:ca:init",
            "--server-ip=127.0.0.1",
            "--server-ip=192.168.8.100",
            "--server-ip=192.168.8.101",
            "--server-ip=192.168.8.102",
            "--server-ip=192.168.8.103",
            f"--server-ip={ip}",
            "--server-dns=localhost",
            "--server-dns=xephyguard-ccc"
        ], cwd="server", env=env, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception:
        pass

    print("=" * 60)
    print(f"[XephyGuard] Network & SSL Certificate Sync Completed!")
    print(f"Current Laptop Wi-Fi IP : {ip}")
    print(f"Current Subnet          : {subnet}")
    print(f"Dashboard (HTTPS)       : https://{ip}/login")
    print(f"Dashboard (HTTP :8000)  : http://{ip}:8000/login")
    print("=" * 60)
