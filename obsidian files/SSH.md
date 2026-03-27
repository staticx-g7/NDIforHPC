here is the jureca ssh command:
```
ssh -v -o ServerAliveInterval=60 -o ServerAliveCountMax=3 -i C:\Users\staticxg7\.ssh\ed_25519_universal_openssh -m hmac-sha2-256-etm@openssh.com,hmac-sha2-512-etm@openssh.com,umac-128-etm@openssh.com george2@login.jupiter.fz-juelich.de  
```

Use this commadn to see whats your compute node you got?:
```
squeue -u $USER
```

SSH into the compute node:
```
ssh -v -L 8000:jpbo-012-39:8000 -o ServerAliveInterval=60 -o ServerAliveCountMax=3 -i C:\Users\staticxg7\.ssh\ed_25519_universal_openssh -m hmac-sha2-256-etm@openssh.com,hmac-sha2-512-etm@openssh.com,umac-128-etm@openssh.com george2@login.jupiter.fz-juelich.de
```
Get inside the compute node as interactive:
```
srun --ntasks=1 --nodelist=jpbo-005-44 --pty bash -i
```

for linux:
```
ssh -v -o ServerAliveInterval=60 -o ServerAliveCountMax=3 \
  -i ~/.ssh/ed_25519_universal_openssh \
  -o MACs=hmac-sha2-256-etm@openssh.com,hmac-sha2-512-etm@openssh.com,umac-128-etm@openssh.com \
  george2@login01.jupiter.fz-juelich.de
```

```
ssh -v -L 8000:jpbo-123-14:8000 \
  -o ServerAliveInterval=60 -o ServerAliveCountMax=3 \
  -i ~/.ssh/ed_25519_universal_openssh \
  -o MACs=hmac-sha2-256-etm@openssh.com,hmac-sha2-512-etm@openssh.com,umac-128-etm@openssh.com \
  george2@login.jupiter.fz-juelich.de
```

```
ssh -L 5961:localhost:5961 -i ~/.ssh/ed_25519_universal_openssh \
  -o MACs=hmac-sha2-256-etm@openssh.com,hmac-sha2-512-etm@openssh.com,umac-128-etm@openssh.com \
  george2@login01.jupiter.fz-juelich.de
```