#!/usr/bin/env python3
"""Teste automatico de boot do Orion OS (so biblioteca padrao do Python).

Para cada forma de boot (GRUB e bootloader proprio) sobe o QEMU sem janela,
digita comandos pelo monitor do QEMU (sendkey) e confere o transcript que o
kernel espelha na porta de debug 0xE9 (-debugcon).

Uso:  python3 tools/test_boot.py [--qemu qemu-system-i386] [--build build]
"""
import argparse, os, shutil, socket, subprocess, sys, tempfile, time

KEYS = {' ': 'spc', '-': 'minus', '.': 'dot', '\n': 'ret', '/': 'alt_r-q'}   # '/' no ABNT2 = AltGr+Q

def keyname(c):
    return KEYS.get(c, c)

class Vm:
    def __init__(self, qemu, build, boot, data=None):
        self.tmp = tempfile.mkdtemp(prefix="orion-test-")
        self.log = os.path.join(self.tmp, "debug.log")
        self.sock = os.path.join(self.tmp, "mon.sock")
        if data is None:                      # disco novo (zerado) so para esta VM
            data = os.path.join(self.tmp, "data.img")
            with open(data, "wb") as f:
                f.write(b"\0" * 1024 * 1024)
        cmd = [qemu, "-machine", "pc", "-m", "128M", "-display", "none",
               "-drive", f"format=raw,file={data},if=ide,index=0",
               "-debugcon", f"file:{self.log}",
               "-monitor", f"unix:{self.sock},server,nowait"]
        if boot == "grub":
            cmd += ["-cdrom", os.path.join(build, "orion.iso"), "-boot", "d"]
        else:
            cmd += ["-drive", f"format=raw,file={os.path.join(build, 'orion.img')},if=floppy",
                    "-boot", "a"]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        for _ in range(100):
            if os.path.exists(self.sock):
                break
            time.sleep(0.1)
        self.mon = socket.socket(socket.AF_UNIX)
        self.mon.connect(self.sock)
        self.mon.settimeout(0.5)

    def text(self):
        try:
            return open(self.log, "rb").read().decode("latin-1")
        except FileNotFoundError:
            return ""

    def wait(self, needle, timeout=40, start=0):
        end = time.time() + timeout
        while time.time() < end:
            t = self.text()
            if needle in t[start:]:
                return True
            if self.proc.poll() is not None:
                return needle in self.text()[start:]
            time.sleep(0.2)
        return False

    def type(self, line):
        for c in line + "\n":
            self.mon.sendall(f"sendkey {keyname(c)}\n".encode())
            time.sleep(0.06)
            try:
                self.mon.recv(4096)
            except socket.timeout:
                pass

    def run(self, line, needle, timeout=40):
        start = len(self.text())
        self.type(line)
        return self.wait(needle, timeout, start)

    def close(self):
        self.proc.kill()
        self.proc.wait()
        shutil.rmtree(self.tmp, ignore_errors=True)

results = []
def check(name, ok, vm=None):
    results.append(ok)
    print(("  [ OK ] " if ok else "  [FAIL] ") + name)
    if not ok and vm:
        print("    ---- fim do transcript ----")
        print("    " + vm.text()[-700:].replace("\n", "\n    "))

# comando de falha -> trecho que o panic deve mostrar
CRASHES = [
    ("crash null",  "ponteiro NULL"),
    ("crash text",  "violacao de protecao"),
    ("crash stack", "ESTOURO DE PILHA"),
    ("crash div",   "divisao por zero"),
    ("crash ud",    "opcode invalido"),
    ("crash heap",  "canario destruido"),
    ("crash free2", "free duplo"),
]

def session(args, boot):
    print(f"\n== boot: {boot} ==")
    vm = Vm(args.qemu, args.build, boot)
    try:
        ok = vm.wait("[ OK ] kernel pronto")
        check("kernel chegou ao shell", ok, vm)
        if not ok:
            return
        t = vm.text()
        check("paginacao ligada", "Paginacao ligada" in t, vm)
        check("heap criado", "heap de" in t, vm)
        check("info mostra o bootloader",
              vm.run("info", "bootloader:") and
              ("GRUB" in vm.text() if boot == "grub" else "Orion Boot" in vm.text()), vm)
        check("mem", vm.run("mem", "vazamento:"), vm)
        check("memmap", vm.run("memmap", "disponivel"), vm)
        check("vmm", vm.run("vmm", "guarda da pilha"), vm)
        check("autoteste da memoria (memtest)",
              vm.run("memtest", "verificacoes", timeout=90) and
              "TODAS as" in vm.text() and "FALHOU" not in vm.text(), vm)
        check("heapcheck deep", vm.run("heapcheck deep", "consistentes"), vm)
        check("format pede confirmacao", vm.run("format", "digite: format sim"), vm)
        check("format sim", vm.run("format sim", "OrionFS formatado", timeout=60), vm)
        check("autoteste do OrionFS (fstest)",
              vm.run("fstest", "verificacoes do FS", timeout=300) and
              "TODAS as" in vm.text()[vm.text().rfind("Autoteste do OrionFS"):] and
              "[FALHOU]" not in vm.text(), vm)
        check("breakpoint e recuperavel", vm.run("crash bp", "breakpoint em EIP"), vm)
        check("shell continua vivo apos #BP", vm.run("info", "paginacao:"), vm)
    finally:
        vm.close()

    for cmd, expect in CRASHES:
        vm = Vm(args.qemu, args.build, boot)
        try:
            if not vm.wait("[ OK ] kernel pronto"):
                check(cmd + " (boot)", False, vm)
                continue
            check(f"{cmd}: panic com '{expect}'", vm.run(cmd, expect, timeout=20), vm)
        finally:
            vm.close()

def fs_session(args, boot):
    """Usa o shell de verdade e prova que os dados sobrevivem a um 'desligamento' brusco."""
    print(f"\n== sistema de arquivos pelo shell: {boot} ==")
    data = tempfile.mktemp(prefix="orion-fsdata-")
    with open(data, "wb") as f:
        f.write(b"\0" * 1024 * 1024)
    try:
        vm = Vm(args.qemu, args.build, boot, data)
        try:
            check("kernel subiu", vm.wait("[ OK ] kernel pronto"), vm)
            check("disco novo avisa que falta formatar",
                  "Disco sem OrionFS v3" in vm.text(), vm)
            check("ls sem formatar da erro claro", vm.run("ls", "nao montado"), vm)
            check("format sim", vm.run("format sim", "OrionFS formatado", timeout=60), vm)
            check("mkdir /docs/sub (2 niveis, 1 por vez)",
                  vm.run("mkdir /docs", "orion:/>") and vm.run("mkdir /docs/sub", "orion:/>"), vm)
            check("cd /docs/sub e prompt mostra a pasta",
                  vm.run("cd /docs/sub", "orion:/docs/sub>"), vm)
            check("pwd", vm.run("pwd", "/docs/sub\n"), vm)
            check("write relativo", vm.run("write nota.txt ola orion fs", "orion:/docs/sub>"), vm)
            check("cat", vm.run("cat nota.txt", "ola orion fs"), vm)
            check("append + cat", vm.run("append nota.txt -mais", "orion:/docs/sub>") and
                  vm.run("cat nota.txt", "ola orion fs-mais"), vm)
            check("cp para pasta acima", vm.run("cp nota.txt ..", "orion:/docs/sub>") and
                  vm.run("cat ../nota.txt", "ola orion fs-mais"), vm)
            check("mv renomeia", vm.run("mv ../nota.txt ../copia.txt", "orion:/docs/sub>") and
                  vm.run("cat /docs/copia.txt", "ola orion fs-mais"), vm)
            check("ls -l mostra permissoes", vm.run("ls -l /docs", "-rw-r--r--"), vm)
            check("stat", vm.run("stat /docs/copia.txt", "tamanho 17 bytes"), vm)
            check("tree", vm.run("tree /", "2 pasta(s), 2 arquivo(s)"), vm)
            check("df", vm.run("df", "OrionFS v3"), vm)
            check("cd inexistente da erro", vm.run("cd /nao/existe", "nao existe"), vm)
            check("rmdir da pasta atual recusa (em uso)", vm.run("rmdir /docs/sub", "em uso"), vm)
            check("cd /", vm.run("cd /", "orion:/>"), vm)
            check("rmdir de pasta cheia recusa", vm.run("rmdir /docs", "nao esta vazio"), vm)
            check("rm -r remove a pasta", vm.run("rm -r /docs/sub", "orion:/>") and
                  vm.run("ls /docs", "1 arquivo(s), 0 pasta(s)"), vm)
            check("sync", vm.run("sync", "Dados gravados"), vm)
        finally:
            vm.close()

        # 2a vida: mesmo disco, VM nova (como religar a maquina)
        vm = Vm(args.qemu, args.build, boot, data)
        try:
            check("remonta sozinho no boot", vm.wait("OrionFS v3 montado"), vm)
            check("sem aviso de volume sujo", "AVISO" not in vm.text(), vm)
            check("arquivo continua la depois de religar",
                  vm.wait("[ OK ] kernel pronto") and vm.run("cat /docs/copia.txt", "ola orion fs-mais"), vm)
            check("fsck limpo", vm.run("fsck", "Volume consistente"), vm)
            # escreve varias coisas e MATA a VM sem sync/desligar
            for i in range(4):
                vm.type(f"write /f{i}.txt conteudo{i}")
                time.sleep(0.4)
            vm.wait("orion:/>", 5)
        finally:
            vm.close()                                  # kill -9: queda de energia

        vm = Vm(args.qemu, args.build, boot, data)
        try:
            check("apos queda de energia o volume monta", vm.wait("OrionFS v3 montado"), vm)
            check("e nao esta sujo (cada operacao fecha a transacao)", "AVISO" not in vm.text(), vm)
            vm.wait("[ OK ] kernel pronto")
            check("fsck limpo apos queda", vm.run("fsck", "Volume consistente"), vm)
        finally:
            vm.close()
    finally:
        if os.path.exists(data):
            os.remove(data)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--qemu", default="qemu-system-i386")
    ap.add_argument("--build", default="build")
    ap.add_argument("--only", choices=["grub", "native"])
    ap.add_argument("--fs-only", action="store_true", help="roda so o teste do sistema de arquivos")
    args = ap.parse_args()

    if not args.fs_only:
        for boot in ([args.only] if args.only else ["grub", "native"]):
            session(args, boot)
    fs_session(args, args.only or "native")

    bad = results.count(False)
    print(f"\n{len(results) - bad}/{len(results)} verificacoes passaram")
    sys.exit(1 if bad else 0)

main()
