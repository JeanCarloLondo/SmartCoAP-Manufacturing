# SETUP.TXT — SmartCoAP-Manufacturing

## Version: 1.0
## Authors: Jean Carlo Londoño Ocampo, Emanuel Gonzales Quintero, Juan Felipe Martínez

# 1. Quick introduction

This document describes the steps required to compile, deploy, and run the CoAP server and clients (console client and ESP32 simulator).
It includes instructions for deploying the server on an AWS EC2 instance (Ubuntu 22.04) and operational recommendations (auto-start, firewall, testing). 
Instructions assume use of **Linux/WSL (Windows Subsystem for Linux)**; WSL-specific notes are included where relevant.

References in the project: README contains additional Makefile targets and usage notes. 

[README](https://github.com/JeanCarloLondo/SmartCoAP-Manufacturing/blob/main/README.md)

---

# 2. Prerequisites (local and EC2)

- OS: Linux (Ubuntu 20.04 / 22.04 recommended) or WSL2 on Windows.

- Tools: gcc, make, sqlite3 (dev headers), git, scp, ssh.

- Ubuntu packages: build-essential, gcc, make, libsqlite3-dev, git, ufw.

- Ports: UDP 5683 (CoAP). Make sure to open this port in the EC2 Security Group.

-  SSH private key (PEM) for accessing the EC2 instance.

---

# 3. Relevant repository structure

- src/ : CoAP implementation (coap.c, coap.h, db.c, db.h). 

[coap](https://github.com/JeanCarloLondo/SmartCoAP-Manufacturing/blob/main/src/coap.c)
[db](https://github.com/JeanCarloLondo/SmartCoAP-Manufacturing/blob/main/src/db.c)

- server.c : CoAP server (pthread concurrency, SQLite). The server creates/uses the DB at ./coap_data.db and listens on port 5683 by default. 

[server](https://github.com/JeanCarloLondo/SmartCoAP-Manufacturing/tree/main/server)

- client.c : Console client (GET, POST, PUT, DELETE). See usage notes in code. 

[client](https://github.com/JeanCarloLondo/SmartCoAP-Manufacturing/blob/main/clients/client.c)

- esp32_sim.c : ESP32-like simulator that periodically sends POSTs (useful for stress tests). 

 [esp32_sim](https://github.com/JeanCarloLondo/SmartCoAP-Manufacturing/blob/main/clients/esp32_sim.c)

- Makefile : targets to build server, client, esp32_sim and tests (see README). 

---

# 4. Compile (local / on EC2)

## 4.1 Using make (recommended)

**From the project root:**

# Clone (if needed)

 ```git clone <repo-url> ```
 
 ```cd SmartCoAP-Manufacturing ```

# Build main binaries (server, client, esp32_sim)

 ```make all  ```
 
# Common targets in README:

- make server
- make client
- make esp32_sim
- make test


**The repository Makefile includes the targets referenced in the README.**

---

## 4.2 Manual compilation (if necessary)

Minimal example to compile the server manually:

```gcc -o coap_server server.c src/coap.c src/db.c -lpthread -lsqlite3```

Adjust source paths and flags to your environment.

# 5. Run locally (testing)

## 5.1 Server

Run in foreground (default port 5683):

**./coap_server 5683 server.log**

or: ```make server```

If no arguments are provided, the server uses port 5683 and the database ./coap_data.db. 

### server

**Run in background with nohup:**

```nohup ./coap_server 5683 server.log 2>&1 & disown```

## 5.2 Console client

From another machine or the same host (use the server IP or localhost):

```./client <SERVER_IP> 5683```

or

```make client```

## Interactive examples:

- GET all
- POST {"temp":22.5,"hum":61.7}
- PUT 3=temp:23.1
- DELETE 3

**See client.c for full syntax and options (supports sensor/<id> URI paths).**

## 5.3 ESP32 simulator

Send N periodic POSTs:

```./esp32_sim <SERVER_IP> 5683 sensor 10 2```

or

```make esp32_sim```

# Format: <IP> <PORT> <uri_path> <runs> <period_sec>

**Use the simulator to generate load or input data.**

---

# 6. Deployment on AWS EC2 (Ubuntu 22.04) — step-by-step

**Note: replace ~/CoAP_KEY.pem, LOCAL_PATH and EC2_IP with your values.**

## 6.1 Local preparation (WSL/Windows)

On Windows + WSL: avoid Windows-inherited permissions by copying the key into your WSL home and setting secure permissions:

# Copy from Windows filesystem into WSL

```cp /mnt/c/Users/USUARIO/Desktop/PT/SmartCoAP-Manufacturing/CoAP_KEY.pem ~/```

```chmod 400 ~/CoAP_KEY.pem```


**This prevents permission errors when using ssh/scp.**

## 6.2 SSH into the EC2 instance

```ssh -i ~/CoAP_KEY.pem ubuntu@EC2_IP```

```(Example EC2_IP: ec2-3-94-191-143.compute-1.amazonaws.com)```

## 6.3 Configure Ubuntu server (first boot)

On the EC2 instance:

# Update packages
sudo apt update && sudo apt upgrade -y

# Install required tools
sudo apt install -y build-essential gcc make libsqlite3-dev git ufw

## 6.4 Create project directory and upload files

On EC2:

```mkdir -p ~/coap_server```

```cd ~/coap_server```

**From your local machine (WSL), upload the project using scp:**

```scp -i ~/CoAP_KEY.pem -r /mnt/c/Users/USUARIO/Desktop/PT/SmartCoAP-Manufacturing/* \```

``` ubuntu@EC2_IP:~/coap_server```

**Repeat for additional instances using their respective IPs.**

## 6.5 Compile on EC2

**On EC2, inside ~/coap_server:**

make all
# or individual builds:
make server
make client
make esp32_sim

---

## 6.6 Firewall and Security Group

**In the AWS console: edit the instance Security Group to allow UDP/5683 (CoAP) only from necessary IP ranges (restrict access when possible).**

On the instance (optional, via ufw):

```sudo ufw allow OpenSSH```

```sudo ufw allow proto udp to any port 5683```

```sudo ufw enable```

```sudo ufw status```

## 6.7 Run server on EC2

On EC2:

# Run in foreground
./coap_server 5683 server.log

# Run in background
nohup ./coap_server 5683 server.log 2>&1 & disown

**The server initializes ./coap_data.db and writes logs to the file provided (e.g., server.log).**

## 6.8 Auto-start with systemd

---

# 7. Tests and verification

## 7.1 Get the public IP of the instance (if unknown)

On the EC2 instance:

``` curl http://checkip.amazonaws.com```

## 7.2 Test from another machine

Point your client or simulator to the EC2 public IP:

# From local/WSL:

```./client EC2_IP 5683```

# Or:

```./esp32_sim EC2_IP 5683 sensor 5 2```

**Client and simulator usage examples are documented in client.c and esp32_sim.c.**

## 7.3 Inspect logs and database

- Logs: tail -f server.log (or journalctl -u coap_server -f with systemd).

-  Database (SQLite): sqlite3 coap_data.db "SELECT * FROM data LIMIT 10;"

---

## 8. Best practices, security and academic notes

- Security: restrict Security Group rules to required IPs and keep the PEM key secure (chmod 400).

- Persistence: if EC2 may be terminated, back up coap_data.db (e.g., snapshots or periodically copy to S3).

- Monitoring: configure log rotation (logrotate) if logs grow large.

- Testing: use esp32_sim to produce load and validate concurrent behavior. Test scripts and additional utilities are described in tests/ and sh_files/ within the README. 

---

# 9. Troubleshooting (common issues)

- SSH permission errors for key: run chmod 400 ~/CoAP_KEY.pem and ensure the key was copied into WSL (avoid Windows-permission inheritance).

- No packets reaching server: verify EC2 Security Group, ufw status, and that the server binds to 0.0.0.0 (server uses INADDR_ANY). Use sudo tcpdump -i any udp port 5683 to inspect traffic. 

- Compile errors: ensure libsqlite3-dev and build-essential are installed.

- Client timeouts: confirm the EC2 public IP (use curl http://checkip.amazonaws.com on the server) and that UDP/5683 is allowed.

---

# 10. Useful commands

# Local -> WSL: copy key and set permissions
cp /mnt/c/Users/USUARIO/Desktop/PT/SmartCoAP-Manufacturing/CoAP_KEY.pem ~/
chmod 400 ~/CoAP_KEY.pem

# Copy project to EC2
scp -i ~/CoAP_KEY.pem -r /mnt/c/Users/USUARIO/Desktop/PT/SmartCoAP-Manufacturing/* \
  ubuntu@EC2_IP:~/coap_server

# SSH
ssh -i ~/CoAP_KEY.pem ubuntu@EC2_IP

# On EC2: install deps, build and run
sudo apt update && sudo apt upgrade -y
sudo apt install -y build-essential gcc make libsqlite3-dev git ufw
cd ~/coap_server
make all
./coap_server 5683 server.log

# From local: test clients
./client EC2_IP 5683
./esp32_sim EC2_IP 5683 sensor 10 2
