# art

Menu externo para FiveM com overlay DirectX 11 (ImGui), ESP, SilentAim, Triggerbot e um conjunto de módulos de evasão.

> **Leia a seção [Aviso](#aviso--o-que-este-projeto-faz-na-sua-máquina) antes de compilar ou executar.**
> Este projeto desativa proteções do Windows por padrão e apaga registros do sistema. Não é um menu comum.

---

## Requisitos

| Item | Versão |
|---|---|
| Visual Studio | 2022 (v143) ou mais recente |
| Plataforma | x64 |
| SDK do Windows | 10.0.26100 ou compatível |
| Git | qualquer versão recente |

## Como compilar

O projeto depende de **dois pacotes NuGet** que não são versionados neste repositório (a pasta `packages/` está no `.gitignore`). Sem eles o build falha com `MSB4019` / "This project references NuGet package(s) that are missing on this computer", porque o `.vcxproj` importa os `.targets` deles.

```bash
git clone <url-do-repo>
cd art
```

Restaure os pacotes — a forma mais simples é pelo próprio Visual Studio (botão direito na solução → *Restore NuGet Packages*). Pela linha de comando:

```bash
nuget install libcurl.NET.x64 -Version 1.4.1.0 -OutputDirectory packages -NonInteractive
nuget install Microsoft.DXSDK.D3DX -Version 9.29.952.8 -OutputDirectory packages -NonInteractive
```

Abra `src.sln`, selecione **Release / x64** e compile. A saída é `src\x64\Release\cashxyz.exe`.

> Se o seu Visual Studio for mais novo que o 2022 e reclamar de `v143`, compile com
> `/p:PlatformToolset=v145` (ou o toolset instalado) em vez de editar o `.vcxproj`:
> `msbuild src\src.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145`

---

## AVISO — o que este projeto faz na sua máquina

`CommandLine` / `main.cpp` chama `Core::Security::g_ArtBypass.Apply()` **antes de qualquer outra coisa**, e o processo registra uma rotina de limpeza no encerramento. Isso significa que, ao executar, o programa:

### 1. Desativa proteções do Windows Defender

`Security/ArtBypass.hpp` escreve em `HKLM` e neutraliza mecanismos em memória:

- **Exclusões de varredura** para a pasta do executável, para a imagem e para `%LOCALAPPDATA%\FiveM` e `\CitizenFX` — via registro e via `Add-MpPreference`.
- **Chaves de tempo real** sob `SOFTWARE\Microsoft\Windows Defender\Real-Time Protection` (`DisableRealtimeMonitoring`, `DisableBehaviorMonitoring`, `DisableIOAVProtection`, `DisableOnAccessProtection`, `DisableScanOnRealtimeEnable`).
- **AMSI** (`amsi.dll!AmsiScanBuffer`) tem o prólogo reescrito para retornar `E_INVALIDARG`, o que faz qualquer varredura em memória reportar "não escaneável".
- **ETW** (`ntdll!EtwEventWrite`) é substituído por um `ret`, desligando o envio de telemetria para consumidores que estejam escutando.

**Consequência:** enquanto isso está ativo, a máquina fica sem detecção em tempo real — e não só para este programa. Qualquer outro arquivo que entre depois também passa. A função `Restore()` existe, mas restaura apenas parte das chaves, e não é chamada automaticamente.

### 2. Apaga rastros de execução do sistema

`destruct.hpp` roda no encerramento (handler de console + `atexit`) e remove:

- **Prefetch** (`C:\Windows\Prefetch`): os `.pf` do próprio executável, com um registro falso plantado no lugar para o diretório parecer íntegro.
- **Registro**: Amcache, ShimCache (`AppCompatCache`), UserAssist, MUICache, BAM/DAM, RunMRU e ComDlg32.
- **Event logs**: Defender/Operational, PowerShell/Operational, Application Experience, CodeIntegrity, AppLocker, Sysmon (se presente), Application e System.
- **Journal USN** do volume `C:` — apagado e recriado.
- **Timestamps**: os do próprio arquivo são copiados de um vizinho na mesma pasta.

**Consequência:** isso é remoção de evidência. Se algo der errado depois — travamento, corrupção, ou uma investigação de verdade — o registro que explicaria o que aconteceu não está mais lá. Apagar event logs é, por si só, um evento que sistemas de monitoramento procuram.

### 3. Lê e escreve na memória de outro processo

O menu usa `ZwReadVirtualMemory` / `ZwWriteVirtualMemory` contra o processo do FiveM, faz pattern scanning no módulo carregado e **patcheia código executável em tempo de execução** (o hook do SilentAim escreve 14 bytes em cima de instruções vivas, suspendendo as threads do jogo durante a escrita).

É exatamente o comportamento que software antivírus existe para detectar. Não é falso positivo.

### 4. Não há verificação de integridade

O código não é assinado. Se você baixou isto de um terceiro, não há nada garantindo que o binário corresponde a este fonte.

---

## Estrutura

```
src/
├── main.cpp                  ponto de entrada; aplica o bypass antes de tudo
├── destruct.hpp              limpeza de artefatos (compilada 1x via Security/Destruct.cpp)
├── Globals.hpp               estado global
├── Core/
│   ├── Config.hpp            configuração persistida
│   ├── Offsets.hpp           offsets e assinaturas por build
│   ├── Core.hpp              SetupOffsets(), inicialização
│   ├── Features/
│   │   ├── Esp.cpp           ESP: box, preenchimento, skeleton, head, barras, nomes, distância
│   │   ├── SilentAim.cpp     hook com code cave
│   │   ├── Triggerbot.cpp    disparo automático
│   │   ├── Aimbot.cpp        aimbot por ângulo de visão
│   │   └── MagicBullets.cpp  patch de posição de projétil
│   ├── SDK/                  leitura de memória, structs do jogo, scanner de assinaturas
│   └── Threads/              threads de atualização (entidades, veículos, nomes, resolução)
├── Gui/
│   ├── Gui.cpp               render do menu
│   ├── Overlay/Overlay.cpp   overlay DirectX 11
│   └── Pages/                páginas (Combat, Visuals, Local, Settings, Login)
├── Includes/
│   ├── CustomWidgets/        widgets próprios, incluindo Branding.hpp
│   └── ImGui/                Dear ImGui + backend DX11/Win32
└── Security/
    ├── ArtBypass.hpp         bypass do Defender (ver aviso acima)
    ├── ScreenShare.hpp       exclusão do overlay de capturas de tela
    ├── Destruct.cpp          unidade de compilação do destruct
    ├── DestructHook.hpp      declaração leve do hook de limpeza
    └── AntiCrack.hpp         verificações anti-análise
```

## Notas de implementação

Pontos que não são óbvios lendo o código:

- **`destruct.hpp` é incluído por um único `.cpp`.** Ele puxa `winioctl.h` e `<filesystem>`; expô-lo pelo header base obrigava todo arquivo do projeto a compilar isso e dobrava o tempo de build. Quem precisa disparar a limpeza inclui `Security/DestructHook.hpp`, que declara só duas funções.
- **A cave do SilentAim tem layout fixo.** O prologue de 14 bytes deslocado é reproduzido na cabeça da cave, seguido do shell que escreve X/Y/Z. Os offsets (3/10/17/27, total 35) estão documentados em `SilentAim.hpp` e precisam bater com os `memcpy` em `SilentAim.cpp`.
- **`Core::SDK::Game::kPedTypePlayer`** é o valor de `CPed` que identifica um jogador. Vale **2** por padrão. Se estiver errado para o seu build, `IgnoreNPCs` vai esconder jogadores reais em vez de NPCs — ative a opção e confira. A correção é essa linha, em `SDK.hpp`.
- **Os mutexes ficam ao lado dos dados que protegem**, em `SDK.hpp` (`EntityListMutex`, `VehicleListMutex`, `FriendMapMutex`). Não declare um mutex local no produtor: leitor e escritor precisam do mesmo objeto, senão a proteção é decorativa.
- **`imgui_demo.cpp` está fora do build** de propósito — não há call site e é o maior arquivo do ImGui.

## Licença

Sem licença definida. Todos os direitos reservados ao autor.
