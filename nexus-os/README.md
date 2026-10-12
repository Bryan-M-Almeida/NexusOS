# NEXUS.OS v0.1

Sistema operacional real, bootável (BIOS legacy), escrito em Assembly (NASM) + C (GCC `-m16`).
Roda em máquina virtual e em pendrive.

## Requisitos atendidos
| Requisito | Como |
|---|---|
| **Ligar** | Boot: bootloader -> kernel -> animação NEXUS, que inicializa o sistema e mostra o log (`[ OK ]`) -> shell. Sem vídeo VBE: tela de texto com barra de carregamento. Em PC real, "ligar" = botão de energia + boot pelo pendrive |
| **Desligar** | Comando `shutdown`: ACPI real (lê FADT/DSDT `_S5_`), com fallback para QEMU/Bochs/VirtualBox e APM |
| **Listar arquivos do armazenamento** | `ls [caminho]`, `tree` |
| **Criar pasta** | `mkdir <pasta>` (aceita caminhos: `mkdir projetos/app`) |
| **Editar pasta** | `rename <de> <para>` (alias `mv`) |
| **Excluir pasta** | `rmdir <pasta>` (vazia) ou `rm -r <pasta>` (com conteúdo) |

Extras mínimos para usar o sistema: `cd`, `pwd`, `touch` (cria arquivo vazio, para o `ls` ter o que listar), `info`, `format`, `clear`, `reboot`, `help`. Seta ↑ repete o último comando.

> Arquivos são só entradas de nome (sem conteúdo): o escopo pedido foi gerenciar pastas, não editar arquivos.

## Animação de abertura NEXUS
Logo após carregar o kernel, o sistema troca para vídeo gráfico (VBE, 32 bpp, 1024x768 -> 800x600 -> 640x480) e toca a intro com a identidade visual NEXUS. A inicialização acontece durante a animação: cada etapa (teclado, disco, NXFS...) aparece como uma linha de log abaixo do status, e o título N.E.X.U.S só entra (`SISTEMA ONLINE`) quando o boot termina. **Qualquer tecla pula** (as etapas que faltarem rodam sem animação). Se a máquina não tiver VBE com framebuffer linear de 32 bpp, ou tiver menos de ~16 MB de RAM, o boot usa a tela de texto clássica.

- `src/intro.c` – `nexus_intro_play()`: double buffering na memória acima de 1 MB (modo *unreal*), tempo real pelo PIT, tudo em ponto fixo
- `tools/mkassets.py` – gera `build/assets.h` (glifos Michroma / JetBrains Mono em 4 bits + tabelas de atan/easing). Fontes em `tools/fonts/`, licença SIL OFL 1.1

## Como rodar
**VirtualBox (mais fácil): use o `nexus.iso`.** Nova VM -> Type *Other*, Version *Other/Unknown (64-bit)* -> no campo **ISO Image** escolha `nexus.iso` -> marque *Skip Unattended Installation* -> desmarque *Enable EFI* -> Start.
No modo ISO o CD é somente leitura: tudo funciona (ls, mkdir, rename, rm, shutdown), mas as alterações valem só até desligar. Para **salvar** as pastas, use o disco (`nexus.vdi` / `nexus.img`).

**QEMU:** `./run.sh` (disco, salva), `make run-iso` (CD) ou `make run-usb` (a ISO como se estivesse gravada num pendrive)

**VirtualBox:** `VBoxManage convertfromraw nexus.img nexus.vdi --format VDI`, crie uma VM (Tipo *Other/Unknown*, **sem EFI**, sem disco novo) e anexe o `nexus.vdi` como disco.

**VMware:** `qemu-img convert -O vmdk nexus.img nexus.vmdk`, e use como disco existente (firmware BIOS).

**Pendrive:** a `nexus.iso` é **híbrida**: o mesmo arquivo dá boot como CD (VirtualBox) e gravado direto num pendrive.
- Windows: Rufus com a `nexus.iso` (ou o `nexus.img`). Se o Rufus perguntar, escolha **"Gravar em modo Imagem DD"**. balenaEtcher / Win32DiskImager também servem.
- Linux: `sudo ./flash.sh /dev/sdX` (grava o `nexus.img`; confira o nome com `lsblk`).
- Gravado em pendrive, o sistema **salva** as pastas (no CD/ISO do VirtualBox as alterações valem só até desligar).
- No PC, boote pelo pendrive em modo **Legacy/CSM** (desative Secure Boot). Não funciona em UEFI puro.
- ⚠ Gravar apaga o pendrive inteiro (a imagem ocupa 1 MiB, o resto fica sem uso).

## Compilar
Precisa de `gcc`, `ld`, `nasm`, `make`, `xorriso`, `python3` com Pillow: `make`  ->  gera `nexus.img` e `nexus.iso`.

No Windows, use o WSL (Ubuntu):
```
sudo apt install build-essential gcc-multilib nasm xorriso python3-pil qemu-system-x86
make
```
As imagens prontas ficam nas *Releases* do GitHub (não são versionadas no repositório).

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
- `src/boot_cd.asm` – boot por CD (El Torito); `src/boot_hyb.asm` – MBR da ISO híbrida (pendrive), carrega o kernel do `NEXUS.IMG` dentro da ISO

## Limitações conhecidas
- BIOS legacy apenas (sem UEFI), 16-bit, kernel < ~60 KB.
- Pendrive em modo "USB-FDD" (raro) não tem LBA estendido: o boot avisa com erro.
- Máx. 224 entradas, nomes de até 29 caracteres sem espaços, sem acentos.
- Testado no QEMU (boot, criar/renomear/excluir, persistência após reiniciar, desligamento via ACPI). **Não testado em hardware físico** — ao testar no PC, comece por um pendrive sem dados.
