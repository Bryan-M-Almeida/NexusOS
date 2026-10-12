# NEXUS.OS v0.1

Sistema operacional real, bootável (BIOS legacy), escrito em Assembly (NASM) + C (GCC `-m16`).
Roda em máquina virtual e em pendrive.

## Requisitos atendidos
| Requisito | Como |
|---|---|
| **Ligar** | Boot: bootloader -> kernel -> tela de abertura com barra de carregamento. Em PC real, "ligar" = botão de energia + boot pelo pendrive |
| **Desligar** | Comando `shutdown`: ACPI real (lê FADT/DSDT `_S5_`), com fallback para QEMU/Bochs/VirtualBox e APM |
| **Listar arquivos do armazenamento** | `ls [caminho]`, `tree` |
| **Criar pasta** | `mkdir <pasta>` (aceita caminhos: `mkdir projetos/app`) |
| **Editar pasta** | `rename <de> <para>` (alias `mv`) |
| **Excluir pasta** | `rmdir <pasta>` (vazia) ou `rm -r <pasta>` (com conteúdo) |

Extras mínimos para usar o sistema: `cd`, `pwd`, `touch` (cria arquivo vazio, para o `ls` ter o que listar), `info`, `format`, `clear`, `reboot`, `help`. Seta ↑ repete o último comando.

> Arquivos são só entradas de nome (sem conteúdo): o escopo pedido foi gerenciar pastas, não editar arquivos.

## Como rodar
**VirtualBox (mais fácil): use o `nexus.iso`.** Nova VM -> Type *Other*, Version *Other/Unknown (64-bit)* -> no campo **ISO Image** escolha `nexus.iso` -> marque *Skip Unattended Installation* -> desmarque *Enable EFI* -> Start.
No modo ISO o CD é somente leitura: tudo funciona (ls, mkdir, rename, rm, shutdown), mas as alterações valem só até desligar. Para **salvar** as pastas, use o disco (`nexus.vdi` / `nexus.img`).

**QEMU:** `./run.sh` (disco, salva) ou `make run-iso` (CD)

**VirtualBox:** `VBoxManage convertfromraw nexus.img nexus.vdi --format VDI`, crie uma VM (Tipo *Other/Unknown*, **sem EFI**, sem disco novo) e anexe o `nexus.vdi` como disco.

**VMware:** `qemu-img convert -O vmdk nexus.img nexus.vmdk`, e use como disco existente (firmware BIOS).

**Pendrive:**
- Linux: `sudo ./flash.sh /dev/sdX` (confira o nome com `lsblk`).
- Windows: Rufus em modo **"Imagem DD"** (ou balenaEtcher / Win32DiskImager) com o `nexus.img`.
- No PC, boote pelo pendrive em modo **Legacy/CSM** (desative Secure Boot). Não funciona em UEFI puro.
- ⚠ Gravar apaga o pendrive inteiro (a imagem ocupa 1 MiB, o resto fica sem uso).

## Compilar
Precisa de `gcc`, `ld`, `nasm`, `python3`: `make`  ->  gera `nexus.img`.

## Arquitetura
```
LBA 0        boot.asm   setor de boot (+ tabela de partição dummy p/ BIOS exigentes)
LBA 1..127   kernel     carregado em 1000:0000 (modo real, segmento de 64 KB)
LBA 128..    NXFS       superbloco + tabela de 224 entradas (tipo, pai, nome[30])
```
- `src/start.asm` – entrada, zera .bss, modo *unreal* (para ler tabelas ACPI acima de 1 MB)
- `src/disk.c` – INT 13h estendido (LBA): é o que permite acessar o pendrive de verdade
- `src/fs.c` – NXFS: montar, formatar, criar, renomear, remover (recursivo), caminhos
- `src/power.c` – desligar (ACPI/VM/APM) e reiniciar
- `src/video.c`, `kbd.c`, `shell.c`, `main.c` – VGA texto, teclado, shell e splash

## Limitações conhecidas
- BIOS legacy apenas (sem UEFI), 16-bit, kernel < ~60 KB.
- Pendrive em modo "USB-FDD" (raro) não tem LBA estendido: o boot avisa com erro.
- Máx. 224 entradas, nomes de até 29 caracteres sem espaços, sem acentos.
- Testado no QEMU (boot, criar/renomear/excluir, persistência após reiniciar, desligamento via ACPI). **Não testado em hardware físico** — ao testar no PC, comece por um pendrive sem dados.
