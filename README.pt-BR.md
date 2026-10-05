# Discord Bridge para open.mp e SA-MP

> English version: [README.md](README.md)

Conecte o seu servidor de open.mp ou SA-MP ao Discord e controle tudo pelo Pawn:
comandos `/`, botões, menus, formulários, mensagens bonitas, moderação e muito
mais.

```pawn
public OnGameModeInit()
{
    DBR_CreateCommand("ping", "Responde com pong", .callback = "Cmd_Ping");
}

forward Cmd_Ping(DiscordInteraction:interaction, DiscordUser:user);
public Cmd_Ping(DiscordInteraction:interaction, DiscordUser:user)
{
    return DBR_RespondInteraction(interaction, "Pong!");
}
```

## Índice

- [Recursos](#recursos)
- [Instalação](#instalação)
  - [1. Instale o plugin](#1-instale-o-plugin)
  - [2. Crie o bot no Discord](#2-crie-o-bot-no-discord)
  - [3. Configure o token](#3-configure-o-token)
  - [Escolhendo os intents](#escolhendo-os-intents)
- [Conceitos em 2 minutos](#conceitos-em-2-minutos)
- [Guia rápido](#guia-rápido)
- [Callbacks](#callbacks)
- [Problemas comuns](#problemas-comuns)
- [Compilando o plugin](#compilando-o-plugin)

## Recursos

| Recurso | O que dá para fazer | Exemplo |
| --- | --- | --- |
| Mensagens | Enviar, responder, editar, apagar, fixar, reagir, limpar canal, DM | [12](examples/pt-BR/12-mensagens.pwn) |
| Embeds | Título, cor, autor, campos, imagens, rodapé | [02](examples/pt-BR/02-embeds.pwn) |
| Slash commands | Opções, escolhas, subcomandos, autocomplete, permissões, menu de contexto | [03](examples/pt-BR/03-slash-commands.pwn) |
| Botões | Todos os estilos, handler por prefixo, atualizar a mensagem | [04](examples/pt-BR/04-botoes.pwn) |
| Menus de seleção | Opções próprias, usuários, cargos e canais | [05](examples/pt-BR/05-menus-de-selecao.pwn) |
| Modais | Formulários com texto, menus e envio de arquivos | [06](examples/pt-BR/06-modais.pwn) |
| Components V2 | Mensagens com caixas, seções, galerias e separadores | [07](examples/pt-BR/07-components-v2.pwn) |
| Interações | Respostas privadas, adiadas, follow-ups, edição, filtro global | [08](examples/pt-BR/08-respostas-de-interacao.pwn) |
| Moderação | Expulsar, banir, desbanir, castigo, apelido, cargos, voz | [09](examples/pt-BR/09-moderacao.pwn) |
| Consultas | Servidor, membros, usuários, cargos e canais | [10](examples/pt-BR/10-consultas.pwn) |
| Perfil do bot | Status, atividade, nome, avatar, banner, descrição, apelido | [11](examples/pt-BR/11-perfil-do-bot.pwn) |

Toda a rede roda em segundo plano: o plugin nunca trava o servidor.

Exemplos completos: [em português](examples/pt-BR/README.md) · [in English](examples/en/README.md).

## Instalação

### 1. Instale o plugin

Baixe o pacote na [página de releases](https://github.com/itsneufox/omp-Discord-Bridge/releases)
e copie os arquivos:

| Servidor | Onde copiar |
| --- | --- |
| open.mp | conteúdo de `components/` na pasta `components/` do servidor |
| SA-MP | conteúdo de `components/` na pasta `plugins/` e `plugins discord-bridge` no `server.cfg` |
| Ambos | conteúdo de `include/` na pasta de includes do compilador |

- Existe um único binário, que funciona nos dois servidores. SA-MP exige a
  versão **32 bits**.
- No Linux, prefira o pacote `static`. O `dynssl` precisa do OpenSSL instalado
  na máquina. Mantenha qualquer `libboost_system.so` ao lado do plugin.

No seu script:

```pawn
#include <discord-bridge>
```

#### Scripts existentes do Discord Connector

Instale o include principal `discord-bridge.inc` da release do bridge.
Copie `discord-dcc-compat.inc` e `discord-connector.inc` do repositório
[omp-Discord-Bridge-compat](https://github.com/itsneufox/omp-Discord-Bridge-compat)
para a mesma pasta de includes, substituindo o `discord-connector.inc` antigo.
Mantenha `#include <discord-connector>`. Também é possível usar
`#include <discord-dcc-compat>` diretamente. Recompile os scripts e carregue o
binário correspondente do **Discord Bridge** no lugar do Discord Connector.
Arquivos `.amx` compilados com o connector original precisam ser recompilados.
Configure o bot conforme as instruções abaixo.

A compatibilidade cobre APIs DCC com equivalente no bridge: nomes, tags,
enums, callbacks, embeds, moderação e comandos. Callbacks assíncronos mantêm
os argumentos originais e os getters `DCC_GetCreated*()`. Comandos mantêm o
campo de texto opcional `arguments`. `DCC_On*` e `DBR_On*` representam o mesmo
public; defina cada evento apenas uma vez por script.

Sem equivalente no bridge, estas funções ficam de fora:
`DCC_GetUserDiscriminator`, `DCC_GetInteractionMentionCount` e
`DCC_GetInteractionMention`. Scripts que usam essas funções precisam de
ajustes. A compatibilidade é de código-fonte para o subconjunto suportado.

### 2. Crie o bot no Discord

1. Acesse o [Discord Developer Portal](https://discord.com/developers/applications)
   e clique em **New Application**.
2. Na aba **Bot**, clique em **Reset Token** e copie o token. Não compartilhe
   esse token com ninguém.
3. Ainda em **Bot**, ative em **Privileged Gateway Intents**:
   - **Server Members Intent** (lista de membros, entrada e saída);
   - **Message Content Intent** (ler o texto das mensagens);
   - **Presence Intent** (status online dos membros).
4. Na aba **OAuth2 > URL Generator**, marque os escopos `bot` e
   `applications.commands`, escolha as permissões que o bot vai usar e abra o
   link gerado para convidá-lo ao seu servidor.

### 3. Configure o token

O token pode ficar em três lugares. Se houver mais de um, vale o primeiro
desta lista:

1. a variável de ambiente `DISCORD_BOT_TOKEN`;
2. a configuração do servidor (`config.json` no open.mp, `server.cfg` no SA-MP);
3. `DBR_ConnectBot` na sua gamemode.

Com token em 1 ou 2, o bot conecta sozinho e `DBR_ConnectBot` é ignorado (o log
do servidor avisa); nesse caso escolha os intents com `discord_bot_intents`.

**open.mp** (`config.json`):

```json
{
    "discord": {
        "bot_token": "SEU_TOKEN",
        "channel_id": "123456789012345678"
    }
}
```

**SA-MP** (`server.cfg`):

```ini
discord_bot_token SEU_TOKEN
discord_channel_id 123456789012345678
```

**Variável de ambiente**:

```sh
export DISCORD_BOT_TOKEN="SEU_TOKEN"
```

**Gamemode** (usada só quando nenhuma das anteriores está definida):

```pawn
public OnGameModeInit()
{
    DBR_ConnectBot("SEU_TOKEN", DISCORD_INTENTS_DEFAULT);
    return 1;
}
```

| Configuração | Variável de ambiente | Para que serve |
| --- | --- | --- |
| `discord_bot_token` | `DISCORD_BOT_TOKEN` | Token do bot (obrigatório) |
| `discord_bot_intents` | `DISCORD_BOT_INTENTS` | Intents do gateway (padrão: todos; veja [Escolhendo os intents](#escolhendo-os-intents)) |
| `discord_channel_id` | `DISCORD_CHANNEL_ID` | Canal devolvido por `DBR_FindConfiguredChannel()` |
| `discord_channel_name` | `DISCORD_CHANNEL_NAME` | Mesmo que acima, pelo nome do canal |
| `discord_check_for_updates` | `DISCORD_CHECK_FOR_UPDATES` | Verifica se há uma nova versão estável no GitHub (ativado por padrão) |

Ligue o servidor. Quando o bot conectar, `DBR_OnReady` é chamado e tudo está
pronto para uso.

Ao iniciar, o plugin verifica em segundo plano se há uma nova versão estável no
GitHub e registra um link para baixá-la. A verificação é opcional e não baixa
nem instala arquivos. Para desativá-la, defina `discord_check_for_updates` como
`false` no `config.json`, use `discord_check_for_updates 0` no `server.cfg` ou
defina `DISCORD_CHECK_FOR_UPDATES=0` no ambiente.

Para desligar o bot, chame `DBR_DisconnectBot()`. Ele fica offline no Discord
na hora e `DBR_OnDisconnected` é chamado; seus comandos continuam registrados,
então `DBR_ConnectBot` reconecta e publica todos de novo.

```pawn
public OnGameModeExit()
{
    DBR_DisconnectBot();
    return 1;
}
```

### Escolhendo os intents

Intents dizem ao Discord quais eventos enviar para o bot. Três deles são
**privilegiados** e também precisam ser ativados no Developer Portal (aba
**Bot** > **Privileged Gateway Intents**); se o bot pedir um que não está
ativado, o Discord recusa a conexão.

| Intent | Privilegiado | Serve para |
| --- | --- | --- |
| `DISCORD_INTENT_GUILDS` | | Servidores, canais e cargos (quase tudo depende dele) |
| `DISCORD_INTENT_GUILD_MEMBERS` | sim | Lista de membros, `DBR_OnGuildMemberAdd/Update/Remove` |
| `DISCORD_INTENT_GUILD_PRESENCES` | sim | Status online dos membros |
| `DISCORD_INTENT_GUILD_MESSAGES` | | `DBR_OnMessageCreate/Update/Delete` nos servidores |
| `DISCORD_INTENT_MESSAGE_CONTENT` | sim | Ler o texto das mensagens |
| `DISCORD_INTENT_GUILD_MESSAGE_REACTIONS` | | `DBR_OnMessageReaction` |
| `DISCORD_INTENT_GUILD_VOICE_STATES` | | `DBR_OnGuildMemberVoiceUpdate` |
| `DISCORD_INTENT_DIRECT_MESSAGES` | | Mensagens enviadas ao bot por DM |

Presets prontos:

| Preset | O que inclui |
| --- | --- |
| `DISCORD_INTENTS_ALL` | Tudo (padrão). Exige os três privilegiados ativados |
| `DISCORD_INTENTS_DEFAULT` | Tudo que não precisa do Developer Portal |
| `DISCORD_INTENTS_NONE` | Nenhum evento. Slash commands, botões e modais continuam funcionando |

```pawn
// Funciona sem ativar nada no Developer Portal:
DBR_ConnectBot("SEU_TOKEN", DISCORD_INTENTS_DEFAULT);

// Só o que você usa; ler o texto das mensagens exige Message Content ativado:
DBR_ConnectBot("SEU_TOKEN", DISCORD_INTENT_GUILDS | DISCORD_INTENT_GUILD_MESSAGES | DISCORD_INTENT_MESSAGE_CONTENT);
```

Quando o token vem da configuração, `discord_bot_intents` recebe o mesmo valor
como número: `53608447` para todos os intents, `53575421` para
`DISCORD_INTENTS_DEFAULT`.

## Conceitos em 2 minutos

### Tudo começa com `DBR_`

Todas as funções e callbacks do plugin usam o prefixo `DBR_` (**D**iscord
**BR**idge). Digite `DBR_` no editor para ver a API inteira.

| Começa com | Significado | Exemplos |
| --- | --- | --- |
| `DBR_Get...` / `DBR_Is...` | Lê dados já carregados | `DBR_GetUserName`, `DBR_IsUserBot` |
| `DBR_Set...` | Altera algo no Discord | `DBR_SetGuildMemberNickname` |
| `DBR_Create...` | Cria algo (embed, botão, comando...) | `DBR_CreateEmbed` |
| `DBR_Fetch...` | Busca dados atualizados no Discord | `DBR_FetchGuildMember` |
| `DBR_On...` | Callback chamado pelo plugin | `DBR_OnCommand` |

Constantes e tags usam `DISCORD_` e `Discord`: `DISCORD_BUTTON_PRIMARY`,
`DiscordUser:`.

### Handles

Servidores, canais, usuários, cargos e mensagens são representados por
*handles* com tag (`DiscordGuild:`, `DiscordChannel:`...). Você obtém um handle
a partir do ID a qualquer momento, até antes de o bot conectar:

```pawn
new DiscordChannel:canal = DBR_FindChannelByID("123456789012345678");
```

- **Getters** (`DBR_Get...`) leem o que já está em memória e são instantâneos.
- **Fetch** (`DBR_Fetch...`) busca no Discord quando o dado ainda não foi
  carregado.

### Callbacks assíncronos

Pedidos ao Discord levam alguns milissegundos. Funções que terminam em
`callback[], format[], ...` chamam a sua função quando a resposta chega. O
**primeiro parâmetro** é o que foi criado ou carregado (0 se falhou); depois vêm
os seus valores:

```pawn
DBR_SendChannelMessage(canal, "Olá!", "AoEnviar", "i", playerid);

forward AoEnviar(DiscordMessage:mensagem, playerid);
public AoEnviar(DiscordMessage:mensagem, playerid)
{
    if (mensagem == DISCORD_INVALID_MESSAGE) return 1; // falhou
    // ...
    return 1;
}
```

Formatos: `i`/`d` inteiro, `f` float, `b` booleano, `s` texto, `a` array
seguido do tamanho.

### Enviar consome

Embeds, message builders e modais são **destruídos ao serem enviados**. Monte,
envie e esqueça. Componentes adicionados a uma linha, builder ou modal passam a
pertencer a ele.

### Erros

Se o Discord recusar algo (falta de permissão, ID errado, limite de uso),
`DBR_OnActionFail` diz o motivo:

```pawn
public DBR_OnActionFail(const action[], http_status, error_code, const message[])
{
    printf("[discord] %s falhou: %s", action, message);
    return 1;
}
```

### Modo debug

Mensagens informativas, como a publicação dos comandos, ficam ocultas por
padrão. Ative durante os testes; avisos e erros aparecem sempre:

```pawn
DBR_SetDebugMode(true);
```

### Acentos e codificação de texto

O Discord usa UTF-8, enquanto scripts de SA-MP e open.mp normalmente guardam
texto em Windows-1252. O plugin converte nos dois sentidos: acentos que você
envia aparecem certos no Discord, e acentos que chegam do Discord aparecem
certos no jogo. Caracteres que o Windows-1252 não representa, como emojis,
chegam como `?`. Se as strings do seu script são UTF-8, desligue a conversão de
entrada:

```pawn
DBR_SetTextEncoding(DISCORD_ENCODING_UTF8);
```

## Guia rápido

### Enviar mensagens e embeds

```pawn
DBR_SendChannelMessage(canal, "Servidor online!");

new DiscordEmbed:embed = DBR_CreateEmbed("Status", "Tudo funcionando.", .colour = 0x57F287);
DBR_AddEmbedField(embed, "Jogadores", "12/100", true);
DBR_SendChannelEmbedMessage(canal, embed);
```

Para juntar texto, várias embeds e componentes, use um **message builder**:

```pawn
new DiscordMessageBuilder:mensagem = DBR_CreateMessageBuilder("Leia as regras:");
DBR_AddBuilderEmbed(mensagem, DBR_CreateEmbed("Regras", "Respeite todos."));
DBR_AddBuilderComponent(mensagem, linhaDeBotoes);
DBR_SendMessage(canal, mensagem);
```

### Slash commands

```pawn
new DiscordCommand:cmd = DBR_CreateCommand("dado", "Rola um dado", .callback = "Cmd_Dado");
new DiscordCommandOption:lados = DBR_AddCommandOption(cmd, DISCORD_OPTION_INTEGER, "lados", "Quantidade de lados");
DBR_SetOptionRange(lados, 2.0, 100.0);

forward Cmd_Dado(DiscordInteraction:interaction, DiscordUser:user);
public Cmd_Dado(DiscordInteraction:interaction, DiscordUser:user)
{
    new lados = 6;
    DBR_GetInteractionOptionInt(interaction, "lados", lados);
    // ...
}
```

- Crie os comandos quando quiser. O plugin publica tudo sozinho quando o bot
  fica pronto e não publica de novo se nada mudou.
- Comandos sem `.callback` chegam em `DBR_OnCommand`.
- Passe um servidor (`DBR_FindGuildByID("ID")`) para registrar só nele: aparece
  na hora, ótimo para testes. Comandos globais podem levar alguns minutos.

> Cada escopo (global ou um servidor) é **substituído por inteiro**. Comandos
> criados por outras ferramentas no mesmo bot e escopo serão removidos.

### Respondendo interações

Toda interação (comando, botão, menu, modal) precisa de resposta:

| Função | Quando usar |
| --- | --- |
| `DBR_RespondInteraction(i, "texto", .ephemeral = true)` | Resposta rápida (privada com `ephemeral`) |
| `DBR_RespondInteractionEmbed` / `DBR_RespondInteractionMessage` | Resposta com embed ou builder |
| `DBR_DeferInteraction(i)` | Vai demorar? Mostra "pensando..." e responda depois |
| `DBR_UpdateInteractionMessage(i, builder)` | Botões e menus: troca a mensagem onde foram clicados |
| `DBR_SendInteractionFollowup` | Mensagem extra depois da resposta |
| `DBR_EditInteractionResponse` / `DBR_DeleteInteractionResponse` | Editar ou apagar a resposta |
| `DBR_ShowModal(i, modal)` | Abrir um formulário |

Se você não responder, o plugin confirma a interação sozinho no fim do
callback. O handle vale por 15 minutos, então dá para responder depois de um
timer ou de uma consulta ao banco de dados.

### Botões e menus

```pawn
new DiscordComponent:linha = DBR_CreateActionRow();
DBR_AddComponent(linha, DBR_CreateButton(DISCORD_BUTTON_SUCCESS, "Aceitar", "aceitar"));
DBR_AddComponent(linha, DBR_CreateButton(DISCORD_BUTTON_LINK, "Site", "https://open.mp"));

public DBR_OnButton(DiscordInteraction:interaction, DiscordUser:user, const custom_id[])
{
    if (!strcmp(custom_id, "aceitar"))
        return DBR_RespondInteraction(interaction, "Obrigado!", .ephemeral = true);
    return 1;
}
```

Menus usam `DBR_CreateSelectMenu` e chegam em `DBR_OnSelectMenu`. Leia as
escolhas com `DBR_GetInteractionValue(interaction, "", valor)`.

Para organizar muitos botões, registre um handler por prefixo:

```pawn
DBR_RegisterHandler(DISCORD_INTERACTION_COMPONENT, "loja:", "AoClicarLoja", true);
```

### Modais

```pawn
new DiscordModal:modal = DBR_CreateModal("denuncia", "Denunciar jogador");
DBR_AddModalTextInput(modal, "nick", "Nick do jogador");
DBR_AddModalTextInput(modal, "motivo", "O que aconteceu?", DISCORD_TEXT_INPUT_PARAGRAPH);
DBR_ShowModal(interaction, modal);

public DBR_OnModalSubmit(DiscordInteraction:interaction, DiscordUser:user, const custom_id[])
{
    new nick[25];
    DBR_GetInteractionValue(interaction, "nick", nick);
    return DBR_RespondInteraction(interaction, "Denúncia recebida!", .ephemeral = true);
}
```

### Components V2

Adicione blocos como `DBR_CreateContainer`, `DBR_CreateSection`,
`DBR_CreateTextDisplay`, `DBR_CreateMediaGallery` e `DBR_CreateSeparator` a um
message builder. A mensagem vira Components V2 automaticamente: o texto do
builder vira o primeiro bloco e embeds não são permitidas. Veja o
[exemplo 07](examples/pt-BR/07-components-v2.pwn).

### Moderação

```pawn
DBR_KickGuildMember(servidor, membro, "Motivo");
DBR_BanGuildMember(servidor, membro, "Motivo", .delete_message_seconds = 86400);
DBR_UnbanGuildMember(servidor, DBR_FindUserByID("123..."));
DBR_SetGuildMemberTimeout(servidor, membro, 600, "Flood");   // 10 minutos
DBR_SetGuildMemberNickname(servidor, membro, "Novo apelido");
DBR_AddGuildMemberRole(servidor, membro, cargo);
```

O cargo do bot precisa estar **acima** do cargo de quem ele vai moderar. Os
motivos aparecem no registro de auditoria.

### Consultas

```pawn
DBR_FetchGuildMember(servidor, membro, "AoCarregar");

forward AoCarregar(DiscordGuild:servidor, DiscordUser:membro);
public AoCarregar(DiscordGuild:servidor, DiscordUser:membro)
{
    new nome[33], entrou[33];
    DBR_GetGuildMemberDisplayName(servidor, membro, nome);
    DBR_GetGuildMemberJoinedAt(servidor, membro, entrou);
    return 1;
}
```

Também existem `DBR_FetchGuild`, `DBR_FetchUser`, `DBR_FetchRole`,
`DBR_FetchChannel` e `DBR_FetchMessage`.

### Perfil do bot

```pawn
DBR_SetBotPresenceStatus(DISCORD_BOT_PRESENCE_IDLE);
DBR_SetBotActivity("12 jogadores online", DISCORD_ACTIVITY_WATCHING);
DBR_SetBotAvatar("bot/avatar.png");       // procurado também em scriptfiles/
DBR_SetBotUsername("Meu Servidor");
```

## Callbacks

| Callback | Quando é chamado |
| --- | --- |
| `DBR_OnReady()` | Bot conectado e servidores carregados |
| `DBR_OnDisconnected()` | Conexão perdida (o plugin reconecta sozinho) |
| `DBR_OnActionFail(action[], http_status, error_code, message[])` | Uma ação foi recusada pelo Discord |
| `DBR_OnInteraction(interaction, user, type)` | Antes de qualquer interação; retorne `0` para bloquear |
| `DBR_OnCommand(interaction, user, command[])` | Comando sem callback próprio |
| `DBR_OnButton(interaction, user, custom_id[])` | Clique em botão |
| `DBR_OnSelectMenu(interaction, user, custom_id[])` | Escolha em menu |
| `DBR_OnModalSubmit(interaction, user, custom_id[])` | Envio de modal |
| `DBR_OnAutocomplete(interaction, user, command[], option[])` | Digitação em opção com autocomplete |
| `DBR_OnMessageCreate/Update/Delete(message)` | Mensagens criadas, editadas ou apagadas |
| `DBR_OnMessageReaction(message, user, emoji, type)` | Reações |
| `DBR_OnGuildMemberAdd/Update/Remove(guild, user)` | Membros que entram, mudam ou saem |
| `DBR_OnGuildMemberVoiceUpdate(guild, user, channel)` | Entrada e saída de canais de voz |
| `DBR_OnGuildCreate/Update/Delete(guild)` | Servidores |
| `DBR_OnGuildRoleCreate/Update/Delete(guild, role)` | Cargos |
| `DBR_OnChannelCreate/Update/Delete(channel)` | Canais |
| `DBR_OnUserUpdate(user)` | Mudança no perfil de um usuário |

Uma interação passa por esta ordem: `DBR_OnInteraction` → callback do comando
ou handler registrado com `DBR_RegisterHandler` → `DBR_OnCommand` /
`DBR_OnButton` / `DBR_OnSelectMenu` / `DBR_OnModalSubmit` /
`DBR_OnAutocomplete`.

A lista completa de funções, com os parâmetros, está no include
`discord-bridge.inc`.

## Problemas comuns

**O bot não conecta.** Confira o token e se os intents privilegiados estão
ativos no Developer Portal. Com o padrão (todos os intents), os três
privilegiados precisam estar ligados. Para conectar sem eles, use
`DBR_ConnectBot(token, DISCORD_INTENTS_DEFAULT)` ou `discord_bot_intents 53575421`.
O log do servidor mostra qual intent falta quando o Discord recusa a conexão.

**O comando não aparece.** Comandos globais podem levar alguns minutos.
Registre no seu servidor para testar e verifique se o bot foi convidado com o
escopo `applications.commands`. Tente `Ctrl+R` no Discord.

**"A interação falhou".** O Discord espera uma resposta em 3 segundos. Não
faça trabalho pesado antes de responder: chame `DBR_DeferInteraction` e
responda depois.

**O texto das mensagens chega vazio.** Ative o **Message Content Intent**.

**Kick/ban não funciona.** Veja `DBR_OnActionFail`. O código `50013` indica
falta de permissão ou cargo do bot abaixo do alvo.

**Os getters retornam 0.** O dado ainda não está em memória. Espere
`DBR_OnReady` ou use a função `DBR_Fetch...` correspondente.

**`Could NOT find OpenSSL` ao configurar.** As dependências não estão
instaladas onde o CMake consegue achar. No Windows, compile com o preset do
vcpkg (`cmake --preset windows-x86`) em vez de `cmake -S . -B build` e confira
se `VCPKG_ROOT` está definida. No Linux, instale `libssl-dev` e
`libboost-system-dev`.

## Compilando o plugin

Clone o repositório com os submódulos, que trazem os SDKs do open.mp e do AMX:

```sh
git clone --recursive https://github.com/itsneufox/omp-Discord-Bridge
cd omp-Discord-Bridge
```

### Windows

As dependências (OpenSSL e Boost) são instaladas automaticamente pelo
[vcpkg](https://github.com/microsoft/vcpkg) a partir do `vcpkg.json`. Você só
precisa do **Visual Studio 2022** com a carga *Desenvolvimento para desktop com
C++*, do **CMake 3.21+** e do vcpkg.

1. Instale o vcpkg uma vez. Use um caminho curto como `C:\vcpkg`: pastas
   muito profundas estouram o limite de 260 caracteres do Windows ao compilar
   as dependências.

   ```powershell
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
   setx VCPKG_ROOT C:\vcpkg
   ```

   Feche e abra o terminal para a variável `VCPKG_ROOT` valer.

2. Compile. Na primeira vez o OpenSSL e o Boost são baixados e compilados, o
   que leva de 10 a 15 minutos; as próximas compilações são rápidas.

   ```powershell
   cmake --preset windows-x86
   cmake --build --preset windows-x86
   ctest --preset windows-x86
   ```

Resultados em `build/windows-x86/`:

- `plugins/Release/discord-bridge.dll`: o plugin, com o OpenSSL embutido, então é
  o único arquivo a copiar (`components/` no open.mp, `plugins/` no SA-MP);
- `pawno/include/discord-bridge.inc`: o include principal. Os includes de
  compatibilidade ficam no [omp-Discord-Bridge-compat](https://github.com/itsneufox/omp-Discord-Bridge-compat).

Servidores Windows são 32 bits, então o preset sempre compila para Win32.

### Linux

```sh
sudo apt install build-essential cmake libssl-dev libboost-system-dev
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Resultados: `build/plugins/discord-bridge.so` e `build/pawno/include/discord-bridge.inc`.
Para a versão 32 bits usada pelo SA-MP, adicione `-DDISCORD_BRIDGE_BUILD_32BIT=ON`
e instale as bibliotecas `:i386` (veja `.github/workflows/build.yml`).

Use `-DDISCORD_BRIDGE_VERSION=X.Y.Z` em qualquer plataforma para definir a versão.

## Agrupar mensagens limitadas pelo Discord (opcional)

O agrupamento fica **desativado por padrão**. Continue usando:

```pawn
DBR_SendChannelMessage(canalChat, "[Jogador] Olá!");
```

Para ativar no `config.json` do open.mp:

```json
{
  "discord_batch_rate_limited": true,
  "discord_batch_interval_ms": 5000
}
```

No `server.cfg` do SA-MP:

```text
discord_batch_rate_limited 1
discord_batch_interval_ms 5000
```

Com a opção ativa, mensagens normais entram na fila REST para envio imediato.
Quando os cabeçalhos de limite do Discord ou uma resposta 429 adiam um envio,
as mensagens de texto pendentes do mesmo canal são unidas por quebras de linha.
O primeiro envio adiado inicia uma janela de **5.000 ms (5 segundos)**; novas
mensagens não reiniciam esse prazo. O envio aguarda tanto essa janela quanto o
prazo informado pelo Discord, que pode ser maior. Ao terminar a fila acumulada,
os envios voltam ao comportamento imediato. O chat Discord → jogo continua
pelo Gateway, sem pausar mensagens recebidas nem a execução do jogo.

Use `discord_batch_rate_limited` como `false` (SA-MP: `0`) para desativar.
Os aliases `discord.batch_rate_limited` e `discord.batch_interval_ms` também
são aceitos. As variáveis de ambiente `DISCORD_BATCH_RATE_LIMITED` e
`DISCORD_BATCH_INTERVAL_MS` têm prioridade sobre as respectivas configurações.
Para ativar pela variável de ambiente, use `true` ou `1`; outros valores
desativam. O intervalo deve ser um inteiro positivo em milissegundos; valores
inválidos usam 5000. Definir apenas o intervalo **não** ativa o agrupamento.
Reinicie o servidor após alterar a configuração.

Cada mensagem combinada tem até 2.000 bytes UTF-8, sem dividir mensagens originais
e mantendo a ordem do canal. Envios com callback ficam separados para que cada
callback receba seu próprio resultado. Embeds, interações e outras operações
REST não são agrupados; operações REST entre mensagens podem separar os lotes.
Canais sem bloqueio continuam enviando.

A fila REST aceita até 8.192 requisições originais pendentes por bot, incluindo
as agrupadas. Retornar `1` significa que o envio entrou na fila, não que foi
entregue; `0` indica entrada inválida, bot parado ou fila cheia. Falhas HTTP são
registradas no log. Desconectar descarta mensagens pendentes. Não é necessário
um native separado; `DCC_SendChannelMessage` tem o mesmo comportamento no include
de compatibilidade.

## Aviso sobre IA

Ferramentas de IA ajudaram em partes do código e da documentação. Revise o
código e teste o plugin no seu servidor antes de usar em produção.

## Licença

Distribuído sob a [licença MIT](LICENSE). O código de terceiros incluído mantém
suas próprias licenças.
