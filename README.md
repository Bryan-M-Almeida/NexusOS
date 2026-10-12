# NEXUS.OS

Sistema operacional real, bootável (BIOS legacy), escrito em Assembly (NASM) + C (GCC `-m16`).
Roda em máquina virtual e em pendrive.

## Requisitos atendidos
| Requisito | Como |
|---|---|
| **Ligar** | Boot: bootloader -> kernel -> animação NEXUS, que inicializa o sistema e mostra o log (`[ OK ]`) -> shell. Sem vídeo VBE: tela de texto com barra de carregamento. Em PC real, "ligar" = botão de energia + boot pelo pendrive |
| **Desligar** | Comando `shutdown`: contagem regressiva animada (`NEXUS desligando em 3 2 1`, **Esc cancela**) e animação de encerramento, depois ACPI real (lê FADT/DSDT `_S5_`), com fallback para QEMU/Bochs/VirtualBox e APM |
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

## Idiomas
O sistema fala **português, inglês e espanhol**. No primeiro boot, um seletor animado aparece dentro dos aros (setas ou 1/2/3 e Enter); enquanto você navega, o status e o log de boot já mudam para o idioma destacado. A escolha fica salva no disco (superbloco do NXFS) e os próximos boots entram direto nela. Para trocar depois: `idioma` (ou `language`) abre o seletor, ou `idioma pt|en|es` troca direto. No CD/ISO (somente leitura) o seletor aparece a cada boot. Sem vídeo VBE, a escolha é feita num menu em modo texto. Tudo o que aparece antes de o sistema saber o idioma (bootloader, início do boot) é em inglês.

O modo texto (shell e boot sem VBE) usa a **paleta NEXUS**: os 16 registradores de cor da VGA são reprogramados com as cores da identidade visual (fundo `#030508`, texto prata, azul `#6fb3ff`, vermelho `#e5322d`).

- `src/lang.c` – todas as mensagens do sistema nos três idiomas (`tr(id)`)
- Glifos e tabelas da intro ficam em `build/assets.bin`, gravado no setor 256 da imagem e carregado para a memória alta no boot (assim não ocupam o segmento de 64 KB do kernel)

## Como usar
Tudo usa **um único arquivo: `nexus.iso`** (gerado pelo `make`, ou baixado das *Releases* do GitHub). A ISO é **híbrida**: dá boot como CD e também gravada direto num pendrive.

**VirtualBox:** nova VM -> Type *Other*, Version *Other/Unknown (64-bit)* -> em **ISO Image** escolha a `nexus.iso` -> marque *Skip Unattended Installation* -> desmarque *Enable EFI* -> Start. Como CD a mídia é somente leitura: tudo funciona, mas as alterações valem até desligar.

**Pendrive:**
- Windows: Rufus com a `nexus.iso`; se perguntar, escolha **"Gravar em modo Imagem DD"** (balenaEtcher também serve).
- Linux: `sudo tools/flash.sh /dev/sdX` (confira o nome com `lsblk`).
- No pendrive o sistema **salva** as pastas e o idioma.
- No PC, dê boot em modo **Legacy/CSM** (desative o Secure Boot). Não funciona em UEFI puro.
- ⚠ Gravar apaga o pendrive inteiro.

**QEMU:** `make run` (como pendrive, salva em `build/usb.img`) ou `make run-iso` (como CD).

## Compilar
Precisa de `gcc`, `ld`, `nasm`, `make`, `xorriso`, `python3` com Pillow. `make` gera a `nexus.iso`; os arquivos intermediários ficam em `build/` (`make clean` apaga tudo).

No Windows, use o WSL (Ubuntu):
```
sudo apt install build-essential gcc-multilib nasm xorriso python3-pil qemu-system-x86
make
```
As ISOs prontas ficam nas *Releases* do GitHub (não são versionadas no repositório).

## Estrutura
```
src/          kernel (C + Assembly)
tools/        mkassets.py (fontes/tabelas da intro), mkimage.py, mkiso.py, flash.sh, fonts/
Makefile      build
nexus.iso     resultado do build (único arquivo para VirtualBox e pendrive)
build/        intermediários (gerado)
```

## Arquitetura
Imagem do disco (`build/nexus.img`, embutida na ISO):
```
LBA 0        boot.asm   setor de boot (+ tabela de partição dummy p/ BIOS exigentes)
LBA 1..127   kernel     carregado em 1000:0000 (modo real, segmento de 64 KB)
LBA 128..    NXFS       superbloco (com o idioma salvo) + tabela de 224 entradas
LBA 256..    assets     glifos e tabelas da intro (carregados para a memória alta)
```
- `src/start.asm` – entrada, zera .bss, modo *unreal* (acesso à memória acima de 1 MB)
- `src/disk.c` – INT 13h estendido (LBA): HD, pendrive e CD
- `src/fs.c` – NXFS: montar, formatar, criar, renomear, remover (recursivo), caminhos
- `src/power.c` – desligar (ACPI/VM/APM) e reiniciar
- `src/video.c`, `kbd.c`, `shell.c`, `main.c` – modo texto (paleta NEXUS), teclado, shell e boot
- `src/intro.c` – animações (boot, seletor de idioma, desligamento); `src/lang.c` – mensagens
- `src/boot_cd.asm` – boot por CD (El Torito); `src/boot_hyb.asm` – MBR da ISO híbrida (pendrive)

## Limitações conhecidas
- BIOS legacy apenas (sem UEFI), 16-bit, kernel limitado a um segmento de 64 KB.
- Pendrive em modo "USB-FDD" (raro) não tem LBA estendido: o boot avisa com erro.
- Máx. 224 entradas, nomes de até 29 caracteres sem espaços, sem acentos.
- Testado no QEMU (boot, criar/renomear/excluir, persistência após reiniciar, desligamento via ACPI). **Não testado em hardware físico** — ao testar no PC, comece por um pendrive sem dados.
