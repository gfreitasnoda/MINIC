# Projeto MINIC

Neste repositório encontram-se: um analisador léxico (*lexer/scanner*), um analisador sintático (*parser*), um analisador semântico, um **gerador de IR**, um **otimizador de IR** e o **driver `minic`** que une todas as etapas, para uma linguagem simples no estilo C, disponíveis em duas implementações equivalentes: **Python** (`scanner.py` / `parser.py` / `analisador_semantico.py` / `gerador_ir.py` / `otimizador_ir.py` / `minic.py`) e **C** (`scanner.c` / `parser.c` / `analisador_semantico.c` / `gerador_ir.c` / `otimizador_ir.c` / `minic.c`). As duas seguem a mesma lógica, a mesma gramática e o mesmo formato de saída — a escolha entre elas é apenas uma questão de linguagem de implementação.

O scanner lê um código-fonte como texto e o transforma em uma sequência de **tokens**, reportando também **erros léxicos** quando encontra caracteres inválidos ou literais mal formados. O parser consome essa sequência de tokens, verifica se ela obedece à gramática da linguagem e monta uma **AST** (árvore sintática abstrata), reportando **erros sintáticos** quando a estrutura do código está incorreta. O analisador semântico percorre a AST, resolve identificadores em uma tabela de símbolos com escopos, verifica tipos e regras de contexto e reporta **erros semânticos** (`SEM001`–`SEM013`). Por fim, o gerador de IR converte a AST anotada em um **código intermediário de três endereços**, o otimizador de IR reduz esse código, e o `minic` coordena o pipeline completo e produz o relatório final.

## Sumário

- [Visão geral](#visão-geral)
- [Como funciona o scanner](#como-funciona-o-scanner)
- [Tokens reconhecidos](#tokens-reconhecidos)
- [Palavras reservadas](#palavras-reservadas)
- [Estrutura de dados do scanner](#estrutura-de-dados-do-scanner)
- [Referência das funções do scanner](#referência-das-funções-do-scanner)
- [Tratamento de erros léxicos](#tratamento-de-erros-léxicos)
- [Exemplo de uso do scanner](#exemplo-de-uso-do-scanner)
- [Saída de exemplo do scanner](#saída-de-exemplo-do-scanner)
- [Como funciona o parser](#como-funciona-o-parser)
- [Gramática reconhecida](#gramática-reconhecida)
- [Estrutura da AST](#estrutura-da-ast)
- [Tratamento de erros sintáticos](#tratamento-de-erros-sintáticos)
- [Exemplo de uso do parser](#exemplo-de-uso-do-parser)
- [Saída de exemplo do parser](#saída-de-exemplo-do-parser)
- [Analisador semântico](#analisador-semântico)
- [Como funciona o analisador semântico](#como-funciona-o-analisador-semântico)
- [Tipos, categorias e tabela de símbolos](#tipos-categorias-e-tabela-de-símbolos)
- [Regras de tipagem](#regras-de-tipagem)
- [Diagnósticos semânticos](#diagnósticos-semânticos)
- [AST anotada](#ast-anotada)
- [Exemplo de uso do analisador semântico](#exemplo-de-uso-do-analisador-semântico)
- [Saída de exemplo do analisador semântico](#saída-de-exemplo-do-analisador-semântico)
- [Gerador de IR](#gerador-de-ir)
- [Como funciona o gerador de IR](#como-funciona-o-gerador-de-ir)
- [Formato da IR](#formato-da-ir)
- [Convenções de nomes e rótulos](#convenções-de-nomes-e-rótulos)
- [Sinalizadores de recursos usados](#sinalizadores-de-recursos-usados)
- [Referência das funções do gerador](#referência-das-funções-do-gerador)
- [Exemplo de uso do gerador de IR](#exemplo-de-uso-do-gerador-de-ir)
- [Otimizador de IR](#otimizador-de-ir)
- [Como funciona o otimizador](#como-funciona-o-otimizador)
- [Transformações implementadas](#transformações-implementadas)
- [Modo -O2: otimização completa](#modo--o2-otimização-completa)
- [Modo -O1: um alvo por vez](#modo--o1-um-alvo-por-vez)
- [Referência das funções do otimizador](#referência-das-funções-do-otimizador)
- [Exemplo de uso do otimizador](#exemplo-de-uso-do-otimizador)
- [Driver minic](#driver-minic)
- [Uso](#uso)
- [Formato da saída](#formato-da-saída)
- [Códigos de saída](#códigos-de-saída)
- [Funções do driver](#funções-do-driver)

## Visão geral

O projeto implementa o *front-end* de um compilador (**análise léxica**, **análise sintática** e **análise semântica**) e uma parte do *back-end*: **geração de código intermediário** (gerador de IR) e **otimização** (otimizador de IR). O driver `minic` encadeia todas as etapas. Tudo existe tanto em Python quanto em C, com comportamento equivalente entre as versões.

Dado um código-fonte, o **scanner**:

1. Percorre o texto caractere a caractere.
2. Identifica o maior *lexema* válido em cada posição (regra de **maximal munch**).
3. Classifica cada lexema em um tipo de token (identificador, número, operador, pontuação, palavra reservada, string, char etc.).
4. Ignora espaços em branco e comentários (de linha e de bloco).
5. Registra erros para símbolos desconhecidos e literais (string/char/comentário de bloco) não terminados, sem interromper a análise.
6. Rastreia posição (linha e coluna) de cada token/erro.
7. Ao final, adiciona um token `EOF` marcando o fim do código.

Em seguida, o **parser** recebe a lista de tokens gerada pelo scanner e:

1. Verifica se a sequência de tokens forma um programa válido segundo a gramática da linguagem (declarações, funções, comandos e expressões).
2. Monta uma **AST** representando a estrutura do programa.
3. Registra erros sintáticos quando encontra um token inesperado no meio de uma produção, sem abortar a análise inteira — ele tenta se recuperar e continuar procurando outros erros.
4. Ao final, se não houver nenhum erro (léxico ou sintático), a AST é considerada válida e pode ser exibida/serializada; havendo qualquer erro, o código inteiro é rejeitado.

As seções a seguir detalham o scanner, o parser, o analisador semântico, o gerador de IR, o otimizador de IR e, por fim, o driver `minic`.

## Como funciona o scanner

O reconhecimento é baseado em uma lista de **padrões regex com prioridade** (`TOKENS`). Para cada posição no código:

- Todos os padrões são testados a partir daquele ponto.
- Vence o padrão que casa com o **lexema mais longo**.
- Em caso de **empate no comprimento**, vence o padrão de **menor valor de prioridade** (0 é mais prioritário que 1).

Isso resolve ambiguidades como:
- `==` deve ser reconhecido como `igualdade`, não como dois `atribuicao` (`=`).
- `++` deve ser reconhecido como `incremento`, não como dois `soma` (`+`).
- Um identificador como `if` deve virar a palavra reservada `IF`, não um `IDENT` genérico (tratado depois, verificando se o lexema está em `PALAVRAS_RESERVADAS`).
- `<=`, `>=`, `!=`, `&&`, `||`, `++`, `--` vencem seus equivalentes de um único caractere por serem mais longos.

Antes de tentar o *maximal munch*, o scanner também verifica se a posição atual começa um comentário de bloco (`/*`) sem fechamento (`*/`) no restante do arquivo — nesse caso, todo o texto até o fim do arquivo é reportado como um único erro `UNTERMINATED_BLOCK_COMMENT`, em vez de gerar um erro por caractere.

Espaços em branco (`espaco`), comentários de linha (`comentario`, iniciados por `//`) e comentários de bloco (`comentario_bloco`, `/* ... */`) são reconhecidos como tokens válidos, mas **descartados** — não aparecem na lista final de tokens.

## Tokens reconhecidos

| Nome interno         | Regex                    | Token gerado  | Observação                      |
|-----------------------|---------------------------|---------------|----------------------------------|
| `num_real`            | `[0-9]+\.[0-9]+`          | `FLOAT_LIT`   | atributo = valor `float`         |
| `num_int`             | `[0-9]+`                  | `INT_LIT`     | atributo = valor `int`           |
| `id`                  | `[a-zA-Z_][a-zA-Z0-9_]*`  | `IDENT` ou palavra reservada | atributo = o próprio nome (se não for reservada) |
| `igualdade`           | `==`                      | `EQ`          |                                   |
| `diferente`           | `!=`                      | `NEQ`         |                                   |
| `menor_igual`         | `<=`                      | `LE`          |                                   |
| `maior_igual`         | `>=`                      | `GE`          |                                   |
| `e_logico`            | `&&`                      | `AND`         |                                   |
| `ou_logico`           | `\|\|`                    | `OR`          |                                   |
| `nao_logico`          | `!`                       | `NOT`         |                                   |
| `atribuicao`          | `=`                       | `ASSIGN`      |                                   |
| `menor`               | `<`                       | `LT`          |                                   |
| `maior`               | `>`                       | `GT`          |                                   |
| `incremento`          | `\+\+`                    | `INCREMENT`   |                                   |
| `decremento`          | `--`                      | `DECREMENT`   |                                   |
| `soma`                | `+`                       | `PLUS`        |                                   |
| `subtracao`           | `-`                       | `MINUS`       |                                   |
| `multiplicacao`       | `*`                       | `STAR`        |                                   |
| `divisao`             | `/`                       | `SLASH`       |                                   |
| `modulo`              | `%`                       | `PERCENT`     |                                   |
| `abre_parenteses`     | `(`                       | `LPAREN`      |                                   |
| `fecha_parenteses`    | `)`                       | `RPAREN`      |                                   |
| `abre_chave`          | `{`                       | `LBRACE`      |                                   |
| `fecha_chave`         | `}`                       | `RBRACE`      |                                   |
| `abre_colchete`       | `[`                       | `LBRACKET`    |                                   |
| `fecha_colchete`      | `]`                       | `RBRACKET`    |                                   |
| `ponto_virgula`       | `;`                       | `SEMICOLON`   |                                   |
| `virgula`             | `,`                       | `COMMA`       |                                   |
| `ponto`               | `.`                       | `DOT`         |                                   |
| `string_lit`          | `"[^"\n]*"`               | `STRING_LIT`  | atributo = conteúdo sem as aspas  |
| `char_lit`            | `'[^'\n]*'`               | `CHAR_LIT`    | atributo = conteúdo sem os apóstrofos |
| `espaco`              | `[ \t\r\n]+`              | *(descartado)*| espaços, tabs, quebras de linha  |
| `comentario_bloco`    | `/\*.*?\*/` (multilinhas) | *(descartado)*| comentário de bloco              |
| `comentario`          | `//[^\n]*`                | *(descartado)*| comentário de linha              |

Ao final da lista de tokens, o scanner sempre adiciona um token `EOF` (lexema vazio, atributo `null`), marcando a posição final do código.

## Palavras reservadas

```
int, float, bool, char, void, if, else, while, for,
return, break, continue, true, false, print, read
```

Quando um `id` casa com uma dessas palavras, o token gerado é o nome em **maiúsculas** (ex.: `if` → `IF`, `return` → `RETURN`), e o atributo é `null`. Identificadores comuns geram o token `IDENT` com o atributo igual ao próprio nome.

## Estrutura de dados do scanner

### Token

Cada token é um dicionário no formato:

```python
{
    "token": "IDENT",       # nome do tipo do token
    "lexeme": "total",      # texto exato encontrado no código-fonte
    "attribute": "total",   # valor semântico (int, float, string, nome, ou None)
    "line": 2,               # linha onde o token começa (1-indexado)
    "column": 9,             # coluna onde o token começa (1-indexado)
}
```

### Erro

Cada erro léxico é um dicionário no formato:

```python
{
    "erro": "UNKNOWN_SYMBOL",  # código do erro
    "lexema": "@",              # texto que causou o erro
    "linha": 3,
    "coluna": 5,
}
```

> Nota: as chaves do token são em inglês (`token`, `lexeme`, `attribute`, `line`, `column`) e as do erro são em português (`erro`, `lexema`, `linha`, `coluna`) — essa é a nomenclatura real usada pelo código, mantida por compatibilidade com o restante do projeto. Na versão em C, os mesmos campos existem como membros de `struct`s equivalentes, mas o formato de saída impresso (JSON por linha) é idêntico ao da versão em Python.

## Referência das funções do scanner

| Função | Descrição |
|---|---|
| `formatar_valor(valor)` | Converte um valor Python (`None`, `str`, número) em uma representação textual estilo JSON, escapando aspas, barras invertidas e caracteres de controle (`\n`, `\r`, `\t`) em strings. |
| `criar_token(...)` | Monta o dicionário de um token. |
| `token_para_texto(token)` | Serializa um token em uma linha de texto no formato `{"token":...,"lexeme":...,"attribute":...,"line":...,"column":...}`. |
| `criar_erro(...)` | Monta o dicionário de um erro léxico. |
| `erro_para_texto(erro)` | Serializa um erro no formato `{"erro":...,"lexema":...,"linha":...,"coluna":...}`. |
| `casar_maximal_munch(codigo, posicao)` | Testa todos os padrões a partir de `posicao` e retorna o nome e o lexema do que **melhor casa** (maior comprimento, prioridade como critério de desempate). |
| `scanner(codigo)` | Função principal: percorre todo o `codigo`, gera a lista de `tokens` (finalizada com um token `EOF`) e a lista de `erros`, atualizando linha/coluna a cada caractere consumido. |
| `main()` | Lê o caminho de um arquivo a partir do argumento de linha de comando (`sys.argv[1]` em Python, `argv[1]` em C), executa o `scanner` sobre o conteúdo do arquivo e imprime cada token e cada erro como uma linha JSON na saída padrão. |

> A referência acima usa os nomes das funções em Python; a versão em C implementa exatamente as mesmas responsabilidades, com nomes de função equivalentes em estilo C.

## Tratamento de erros léxicos

O scanner nunca trava ou aborta a análise: sempre que encontra algo que não reconhece, registra um erro e continua a partir do próximo ponto possível. Os códigos de erro possíveis são:

| Código | Quando ocorre | Como o scanner se recupera |
|---|---|---|
| `UNKNOWN_SYMBOL` | Um caractere que não casa com nenhum padrão (ex.: `@`, `#`, `$`). | Avança **um caractere** e continua. |
| `UNTERMINATED_STRING_LITERAL` | Uma `"` sem uma `"` de fechamento antes de uma quebra de linha ou do fim do arquivo. | Consome a abertura e os caracteres alfanuméricos/espaços seguintes, e continua a partir daí (permitindo detectar outros tokens/erros na mesma linha). |
| `UNTERMINATED_CHAR_LITERAL` | Um `'` sem um `'` de fechamento antes de uma quebra de linha ou do fim do arquivo. | Consome o `'` e, se houver, o próximo caractere, e continua a partir daí. |
| `UNTERMINATED_BLOCK_COMMENT` | Um `/*` sem um `*/` correspondente em nenhum ponto restante do arquivo. | Consome o **restante inteiro do arquivo** como um único erro (não há mais nada a tokenizar depois disso). |

Isso permite que o scanner relate **todos** os erros léxicos de um código em uma única passada, em vez de parar no primeiro problema.

## Exemplo de uso do scanner

Em Python, importando as funções diretamente:

```python
from scanner import scanner, token_para_texto, erro_para_texto

codigo = """
int x = 10;
float y = 3.14;
if (x > y && !flag) {
    print(x);
}
"""

tokens, erros = scanner(codigo)

for tok in tokens:
    print(token_para_texto(tok))

for err in erros:
    print(erro_para_texto(err))
```

Executando o arquivo diretamente pela linha de comando, informando o caminho de um arquivo de código-fonte como argumento:

```bash
python3 scanner.py caminho/para/arquivo.c
```

Ou, usando a versão em C (após compilar):

```bash
gcc -o scanner scanner.c
./scanner caminho/para/arquivo.c
```

As duas formas produzem exatamente a mesma saída, linha a linha.

## Saída de exemplo do scanner

Para o seguinte código de teste:

```c
int main() {
    int total = 2 + 3 * 4;
    print(total);
    return 0;
}
```

A saída inclui:

```
{"token":"INT","lexeme":"int","attribute":null,"line":1,"column":1}
{"token":"IDENT","lexeme":"main","attribute":"main","line":1,"column":5}
{"token":"LPAREN","lexeme":"(","attribute":null,"line":1,"column":9}
{"token":"RPAREN","lexeme":")","attribute":null,"line":1,"column":10}
{"token":"LBRACE","lexeme":"{","attribute":null,"line":1,"column":12}
{"token":"INT","lexeme":"int","attribute":null,"line":2,"column":5}
{"token":"IDENT","lexeme":"total","attribute":"total","line":2,"column":9}
{"token":"ASSIGN","lexeme":"=","attribute":null,"line":2,"column":15}
{"token":"INT_LIT","lexeme":"2","attribute":2,"line":2,"column":17}
{"token":"PLUS","lexeme":"+","attribute":null,"line":2,"column":19}
...
{"token":"EOF","lexeme":"","attribute":null,"line":6,"column":1}
```

Como esse código não possui símbolos inválidos, nenhuma linha de erro é impressa.

Já para um código com problemas léxicos, como:

```c
x = a @ "sem fechar
y = 'z
```

A saída de erros seria:

```
{"erro":"UNKNOWN_SYMBOL","lexema":"@","linha":1,"coluna":7}
{"erro":"UNTERMINATED_STRING_LITERAL","lexema":"\"sem fechar","linha":1,"coluna":9}
{"erro":"UNTERMINATED_CHAR_LITERAL","lexema":"'z","linha":2,"coluna":5}
```

---

# Parser

O parser (`parser.py` / `parser.c`) implementa a etapa de **análise sintática**, logo após o scanner. Ele consome a lista de tokens gerada pelo scanner e constrói uma **AST** (árvore sintática abstrata) representando a estrutura do programa, ou reporta os erros sintáticos encontrados.

Assim como o scanner, o parser existe em duas implementações (Python e C) com a **mesma lógica, a mesma gramática e o mesmo formato de saída** — por isso ele é descrito aqui de forma única, sem separar por linguagem.

## Como funciona o parser

O parser é um **parser descendente recursivo** (*recursive descent*): cada construção da linguagem (programa, declaração, comando, expressão) é implementada por uma função própria, que consome tokens da posição atual e, quando necessário, chama outras funções para reconhecer sub-construções.

Pontos centrais do funcionamento:

- O parser mantém um cursor (posição atual na lista de tokens) e nunca volta atrás — ele sempre avança, token a token, à medida que reconhece a entrada. (Na versão em C, o aborto de uma produção é feito com `setjmp`/`longjmp`; na versão em Python, com uma exceção interna `ErroSintatico`. O efeito é o mesmo.)
- Há **limites de aninhamento** compartilhados pelas duas implementações: `MAX_PROF_PARSE = 1000` (chamadas aninhadas de `parse_comando`, `parse_atribuicao` e `parse_unario`) e `MAX_PROF_AST = 10000` (profundidade da renderização da AST). Se qualquer um for excedido, o programa aborta com a mensagem `Erro: limite de aninhamento excedido` (em stderr) e código de saída `1`, em vez de depender da pilha de cada runtime.
- Cada função de produção verifica o(s) token(s) esperado(s) na posição atual (`verifica`) e, se corresponderem, avança (`avancar`/`casar`); caso o token esperado não apareça, um erro sintático é registrado e a produção atual é abortada (`esperar`, que lança uma exceção/sinal de erro sintático interno).
- As **expressões** são resolvidas por uma cadeia de funções que respeita a **precedência de operadores**, da menor para a maior precedência: atribuição → OU lógico (`||`) → E lógico (`&&`) → igualdade (`==`, `!=`) → relacional (`<`, `<=`, `>`, `>=`) → aditiva (`+`, `-`) → multiplicativa (`*`, `/`, `%`) → unária (`-`, `!`) → pós-fixa (chamada de função `f(...)` e indexação de vetor `v[...]`) → primária (identificadores, literais, `true`/`false`, expressão entre parênteses). Operadores binários são associativos à esquerda.
- O `parse_primario`/equivalente é onde a recursão "toca o chão": ele reconhece identificadores, literais numéricos, string, char, booleanos e agrupamentos `(...)` — se nada disso casar, é um erro sintático.
- Quando um erro é encontrado, o parser não para: ele registra o erro e **sincroniza** (avança tokens até encontrar um ponto seguro para retomar a análise, como o início de uma declaração, um `;`, um `}` ou palavras-chave de comando), permitindo detectar múltiplos erros em uma única passada — a mesma filosofia de recuperação usada no scanner.
- Ao final, o resultado só é considerado válido se **não houver nenhum erro léxico nem sintático**; havendo qualquer um dos dois, a entrada inteira é rejeitada (não é impressa nenhuma AST parcial).

## Gramática reconhecida

De forma simplificada (notação livre, não EBNF estrita):

```
programa        -> declaracao_global* EOF

declaracao_global -> tipo IDENT ( "(" parametros? ")" bloco        // função
                    | tipo IDENT ( "[" expressao "]" | ( "=" expressao )? ) ";"  // variável (com ou sem vetor/inicializador)
                    | comando

tipo            -> "int" | "float" | "bool" | "char" | "void"
parametros      -> parametro ( "," parametro )*
parametro       -> tipo IDENT ( "[" "]" )?                         // "[]" marca parâmetro vetor

for_init        -> tipo IDENT ( "=" expressao )?                   // declaração (sem vetor)
                 | expressao

bloco           -> "{" ( declaracao_local | comando )* "}"
declaracao_local -> tipo IDENT ( "[" expressao "]" | ( "=" expressao )? ) ";"

comando         -> bloco
                 | "if" "(" expressao ")" comando ( "else" comando )?
                 | "while" "(" expressao ")" comando
                 | "for" "(" for_init? ";" expressao? ";" expressao? ")" comando
                 | "return" expressao? ";"
                 | "break" ";"
                 | "continue" ";"
                 | "print" "(" expressao ")" ";"
                 | "read" "(" alvo ")" ";"
                 | expressao? ";"

expressao       -> atribuicao
atribuicao      -> ou_logico ( "=" atribuicao )?
ou_logico       -> e_logico ( "||" e_logico )*
e_logico        -> igualdade ( "&&" igualdade )*
igualdade       -> relacional ( ( "==" | "!=" ) relacional )*
relacional      -> aditiva ( ( "<" | "<=" | ">" | ">=" ) aditiva )*
aditiva         -> multiplicativa ( ( "+" | "-" ) multiplicativa )*
multiplicativa  -> unaria ( ( "*" | "/" | "%" ) unaria )*
unaria          -> ( "-" | "!" ) unaria | posfixo
posfixo         -> primario ( "(" argumentos? ")" | "[" expressao "]" )*
primario        -> IDENT | INT_LIT | FLOAT_LIT | STRING_LIT | CHAR_LIT
                 | "true" | "false" | "(" expressao ")"
```

O alvo de um `read(...)` precisa ser uma variável simples ou uma posição de vetor (`Id` ou `Index`); qualquer outra expressão nesse lugar gera erro sintático.

## Estrutura da AST

O parser monta a AST como uma árvore de nós; cada nó carrega seu tipo (`node`), a posição onde a construção começa (`linha`/`coluna`, quando aplicável) e campos específicos do tipo de nó. Os principais tipos de nó são:

| Nó | Campos | Representa |
|---|---|---|
| `Program` | `declaracoes` | O programa inteiro (lista de funções/variáveis globais/comandos). |
| `Function` | `tipo`, `nome`, `params`, `corpo` | Declaração de função. |
| `Param` | `tipo`, `nome`, `vetor` | Parâmetro de função. `vetor` é booleano: verdadeiro quando declarado como `tipo IDENT[]`. |
| `VarDecl` | `tipo`, `nome`, `tamanho`, `valor` | Declaração de variável (global ou local), com vetor (`tamanho`) ou inicializador (`valor`) opcionais. |
| `Block` | `itens` | Bloco `{ ... }` (comandos e declarações locais). |
| `If` | `condicao`, `entao`, `senao` | Comando condicional. |
| `While` | `condicao`, `corpo` | Laço `while`. |
| `For` | `init`, `condicao`, `incremento`, `corpo` | Laço `for`. |
| `Return` | `valor` | Comando `return` (valor opcional). |
| `Break` / `Continue` | — | Comandos `break`/`continue`. |
| `Print` | `valor` | Comando `print(...)`. |
| `Read` | `alvo` | Comando `read(...)`. |
| `ExprStmt` | `expressao` | Comando-expressão (expressão seguida de `;`, ou `;` vazio). |
| `Assign` | `alvo`, `valor` | Atribuição. |
| `Binary` | `op`, `esquerda`, `direita` | Operação binária. |
| `Unary` | `op`, `operando` | Operação unária (`-`, `!`). |
| `Call` | `callee`, `argumentos` | Chamada de função. |
| `Index` | `base`, `indice` | Indexação de vetor `v[i]`. |
| `Id` | `nome` | Referência a identificador. |
| `Lit` | `tipo`, `valor` | Literal (`int`, `float`, `string`, `char`, `bool`). |

Quando a AST é impressa/serializada, cada nó é renderizado em um formato textual do tipo `Tipo(campo1,campo2,...)`, chamando-se recursivamente para os filhos — por exemplo, `Binary(+,Id(x),Lit(int,2))`. Filhos ausentes (por exemplo, o `else` de um `If` ou a condição de um `For`) são renderizados como `NULL`.

Detalhes da renderização:

- `Lit`: literais `float` aparecem como `real` (`Lit(real,3.14)`); `string` é impresso como string JSON entre aspas duplas; `char` fica entre apóstrofos, com `'` interno escapado como `\'`; `bool` aparece como `true`/`false`; `int` é impresso sem zeros à esquerda.
- `Function`: os parâmetros aparecem como `tipo nome`, separados por vírgula, e o corpo vem logo depois (`Function(int soma(int a,int b) Block(...))`). A marca `vetor` de `Param` não é exibida na renderização textual.
- Nós `Assign`/`Binary`/`Call`/`Index` guardam `linha`/`coluna` de referência: `Assign` usa a posição do alvo; `Binary`, o operador; `Call` e `Index`, o `(` e o `[`, respectivamente.

Se qualquer limite de aninhamento (veja [Como funciona o parser](#como-funciona-o-parser)) for excedido durante a análise ou a renderização, a execução é abortada com `Erro: limite de aninhamento excedido`.

## Tratamento de erros sintáticos

O parser, assim como o scanner, nunca aborta a análise no primeiro erro: ao encontrar um token inesperado, ele registra um erro e tenta se recuperar avançando até um ponto de sincronização seguro (início de uma declaração/tipo, `;`, `}`, `EOF` ou uma palavra-chave que inicia um novo comando), continuando a busca por outros erros a partir dali.

Cada erro sintático é registrado no formato:

```python
{
    "erro": "SYNTAX_ERROR",
    "lexema": "}",
    "linha": 4,
    "coluna": 1,
    "mensagem": "token inesperado '}'; esperado SEMICOLON; contexto: termino de declaracao",
}
```

- `lexema`/`linha`/`coluna` identificam o token que causou o problema.
- `mensagem` descreve, de forma legível, o que era esperado e em qual contexto da gramática o erro ocorreu.

Se, ao final da análise, houver **qualquer erro léxico (do scanner) ou sintático (do parser)**, o parser não imprime nenhuma AST: ele indica que a entrada deve ser rejeitada por inteiro. Só quando o código está totalmente livre de erros a AST é exibida.

## Exemplo de uso do parser

Em Python, importando as funções diretamente:

```python
from parser import formatar_resultado

codigo = """
int soma(int a, int b) {
    return a + b;
}

int main() {
    int total = soma(2, 3);
    print(total);
    return 0;
}
"""

print(formatar_resultado(codigo))
```

Quando se deseja a AST e as listas de erros separadamente (como faz o analisador semântico), usa-se `analisar(codigo)`, que devolve `(ast, erros_lexicos, erros_sintaticos)`:

```python
from parser import analisar

ast, erros_lexicos, erros_sintaticos = analisar(codigo)
```

Executando o arquivo diretamente pela linha de comando, informando o caminho de um arquivo de código-fonte como argumento:

```bash
python3 parser.py caminho/para/arquivo.c
```

Ou, usando a versão em C (após compilar):

```bash
gcc -o parser parser.c
./parser caminho/para/arquivo.c
```

As duas formas produzem exatamente a mesma saída.

## Saída de exemplo do parser

Para o código de teste usado como exemplo do scanner:

```c
int main() {
    int total = 2 + 3 * 4;
    print(total);
    return 0;
}
```

A saída do parser (com o código livre de erros) é a AST renderizada em uma única linha de texto:

```
Program(Function(int main() Block(VarDecl(int total=Binary(+,Lit(int,2),Binary(*,Lit(int,3),Lit(int,4)))), Print(Id(total)), Return(Lit(int,0)))))
```

Já para um código com um erro sintático, como:

```c
int main() {
    int total = 2 +
}
```

a saída é apenas a mensagem de rejeição, já que a expressão após `+` está incompleta:

```
NÃO HÁ AST: o parser deve rejeitar a entrada.
```

---

# Analisador semântico

O analisador semântico (`analisador_semantico.py` / `analisador_semantico.c`) implementa a etapa de **análise semântica**, logo após o parser. Ele recebe a AST produzida pelo parser, constrói a **tabela de símbolos** (com escopos aninhados), verifica tipos, uso de identificadores e regras de contexto, e reporta **diagnósticos semânticos** com código (`SEM001`–`SEM013`), linha e coluna.

Assim como as etapas anteriores, existe em Python e em C, com o mesmo comportamento e o mesmo formato de saída. A versão em C inclui `parser.c` (que por sua vez inclui `scanner.c`), portanto os três arquivos `.c` devem estar na mesma pasta; a versão em Python importa o `parser.py` do projeto (e, por ele, o `scanner.py`).

## Como funciona o analisador semântico

O pipeline completo é: **scanner → parser → analisador semântico**.

1. O código-fonte é lido, tokenizado e analisado sintaticamente.
2. Se houver **qualquer erro léxico ou sintático**, a análise semântica **não é executada**: são impressas as linhas `ERRO LÉXICO l:c: ...` e `ERRO SINTÁTICO l:c: ...`, seguidas de `Análise semântica não executada (entrada rejeitada pelo scanner/parser).`, e o programa termina com código `2`.
3. Caso contrário, a análise ocorre em duas fases:
   - **Fase 1 – coleta de globais:** todas as funções e variáveis globais de nível superior são declaradas no escopo global, na ordem em que aparecem (declarações duplicadas geram `SEM002`).
   - **Fase 2 – análise em ordem:** cada item de nível superior é analisado em sequência (funções, inicializações de globais e comandos soltos), com cada função percorrendo seu corpo, resolvendo identificadores e checando tipos.
4. Os diagnósticos são ordenados por **linha, coluna e ordem de emissão** e impressos, seguidos do resumo final.

Regras de escopo e visibilidade:

- Os **parâmetros e o corpo** de uma função compartilham o mesmo escopo (como em C). Cada `Block` e cada `for` abrem um novo escopo.
- Cada símbolo tem uma ordem de visibilidade (`visivel_desde`): uma **função** é visível a partir da sua própria declaração (permitindo recursão, mas não chamada por funções anteriores a ela); uma **variável global** só é visível depois da sua declaração; variáveis locais só são visíveis depois de declaradas.
- Usar um identificador declarado apenas mais adiante gera `SEM001` com mensagem indicando a linha da declaração tardia; usá-lo na própria inicialização (`int x = x + 1;`) também gera `SEM001`, com mensagem específica.
- Quando um tipo já está em estado de erro (`<erro>`), os erros dele derivados não são repetidos (evita cascata de diagnósticos).

Análise de fluxo de retorno: uma função não `void` deve retornar em **todos** os caminhos. Um `return` no nível do bloco conta; um `if` só conta se **ambos** os ramos (`then` e `else`) retornam; `while` e `for` nunca garantem retorno. Caso contrário, é emitido `SEM011`.

## Tipos, categorias e tabela de símbolos

**Tipos:** `int`, `float`, `bool`, `char`, `void` e `string` (apenas como tipo de literal). Vetores têm tipo `T[]` (por exemplo, `int[]`). O tipo especial `<erro>` marca expressões já inválidas.

**Categorias de símbolo:** `variavel`, `parametro`, `vetor` (variável declarada com tamanho, ou parâmetro `tipo nome[]`) e `funcao`.

Cada símbolo guarda:

| Campo | Descrição |
|---|---|
| `nome`, `categoria`, `tipo` | Identificação do símbolo (para funções, `tipo` é o tipo de retorno). |
| `parametros` | Para funções: lista de pares `(tipo, nome)`; parâmetros vetor aparecem com tipo `T[]`. |
| `tipo_retorno` | Para funções: tipo de retorno. |
| `linha`, `coluna` | Posição da declaração. |
| `visivel_desde` | Ordem a partir da qual o símbolo é visível (veja acima). |
| `tamanho_const` | Para vetores: tamanho, quando é uma constante inteira (literal ou `-literal`). |

Na versão em C, tamanhos e índices constantes são tratados como inteiros decimais de **precisão arbitrária** (comparados como texto), de modo a coincidir com os inteiros ilimitados do Python.

## Regras de tipagem

**Compatibilidade de atribuição** (inicialização, `=`, argumentos e retorno): o tipo de destino deve ser igual ao de origem (`int`, `float`, `bool`, `char` ou o vetor correspondente `T[]`), ou então `int` → `float` (única conversão implícita permitida, registrada como `int->float`). `string` não é atribuível a variáveis.

**Operadores binários:**

| Operador(es) | Operandos exigidos | Resultado |
|---|---|---|
| `+` `-` `*` `/` | numéricos (`int`/`float`) | `float` se algum for `float`, senão `int` |
| `%` | ambos `int` | `int` |
| `<` `<=` `>` `>=` | numéricos, ou ambos `char` | `bool` |
| `==` `!=` | numéricos, ou ambos do mesmo tipo `bool`/`char` | `bool` |
| `&&` `||` | ambos `bool` | `bool` |

**Operadores unários:** `-` exige operando numérico (resultado do mesmo tipo); `!` exige `bool`.

**Outras regras:**

- Condições de `if`, `while` e `for` devem ser `bool`.
- Um vetor ou uma função **não** pode ser usado como valor (`v` sozinho, `f` sem parênteses); um vetor só pode ser passado inteiro como **argumento** a um parâmetro vetor de mesmo tipo (`T[]`).
- O alvo de `=` e de `read(...)` deve ser uma variável/parâmetro escalar ou um elemento de vetor (`v[i]`).
- Variáveis e parâmetros não podem ter tipo `void`.
- Uma chamada a função `void` só pode aparecer como comando-expressão (ou no incremento do `for`); usada como valor, gera `SEM012`.
- O tamanho de um vetor deve ser `int`, e, quando constante, positivo; índices constantes são verificados contra o tamanho conhecido.

## Diagnósticos semânticos

| Código | Categoria | Quando ocorre |
|---|---|---|
| `SEM001` | não declarado | Identificador não declarado no escopo, declarado só mais adiante ou usado na própria inicialização. |
| `SEM002` | duplicado | Nome já declarado no mesmo escopo (a mensagem indica a declaração anterior). A posição reportada é a do nome, não a do tipo. |
| `SEM003` | incompatível | Atribuição/inicialização com tipos incompatíveis; variável ou parâmetro `void`; vetor ou função usado como valor ou como destino; destino de atribuição/leitura inválido. |
| `SEM004` | operador | Operandos de tipo inadequado para um operador binário ou unário. |
| `SEM005` | condição | Condição de `if`/`while`/`for` que não é `bool`. |
| `SEM006` | índice | Indexação de algo que não é vetor; índice não `int`; índice constante fora da faixa; tamanho de vetor não `int` ou não positivo. |
| `SEM007` | chamada | Chamada de algo que não é função, ou número de argumentos diferente do esperado. |
| `SEM008` | argumento | Tipo de um argumento incompatível com o do parâmetro. |
| `SEM009` | retorno | Tipo do `return` incompatível com a função; `return` com valor em função `void`; `return` sem valor em função não `void`. |
| `SEM010` | contexto | `return` fora de função; `break`/`continue` fora de laço. |
| `SEM011` | queda | Função não `void` que pode terminar sem retornar valor. |
| `SEM012` | void | Função `void` usada como valor de expressão. |
| `SEM013` | destino não atribuível | Atribuição cujo alvo é um literal inteiro. |

## AST anotada

Durante a análise, cada nó da AST recebe um campo `sem` com informações semânticas. Com a opção `--ast`, a AST anotada é impressa em JSON (indentação de 2 espaços) antes do resumo final. Principais anotações:

| Nó | Campos em `sem` |
|---|---|
| Qualquer expressão | `tipo` (tipo resultante, ou `<erro>`) |
| `Lit` | `valor` |
| `Id` | `simbolo`, `categoria`, `tipo` |
| `Binary` | `tipo_esquerda`, `tipo_direita`, `conversoes` (ex.: `"esquerda int->float"`) |
| `Unary` | `tipo_operando`, `conversoes` |
| `Assign` | `tipo_destino`, `tipo_valor`, `atribuivel`, `compativel`, `conversao` |
| `Index` | `tipo_vetor`, `tipo_indice`, `tipo_elemento` |
| `Call` | `simbolo`, `tipos_argumentos`, `assinatura`, `tipo_retorno` |
| `VarDecl` / `Param` | `simbolo`, `tipo_declarado`, e (se houver inicializador) `tipo_valor`, `compativel`, `conversao` |
| `Function` | `simbolo`, `assinatura` (ex.: `(int,float)->int`) |
| `If` / `While` / `For` | `tipo_condicao` |
| `Return` | `tipo_esperado`, `tipo_efetivo`, `conversao` |
| `Break` / `Continue` | `loop_depth` |
| `Read` | `tipo_alvo` |
| `Program` | `simbolos_globais` (resumo dos símbolos do escopo global) |

Símbolos são serializados em JSON de forma resumida (`nome`, `categoria`, `tipo`, `linha`, `coluna` e, para funções, `parametros` e `tipo_retorno`).

## Exemplo de uso do analisador semântico

Executando pela linha de comando, informando o arquivo de código-fonte (a opção `--ast` é opcional):

```bash
python3 analisador_semantico.py caminho/para/arquivo.c [--ast]
```

Ou, usando a versão em C (após compilar; `scanner.c` e `parser.c` precisam estar na mesma pasta):

```bash
gcc -o analisador_semantico analisador_semantico.c -lm
./analisador_semantico caminho/para/arquivo.c [--ast]
```

Em Python, também é possível usar o pipeline diretamente:

```python
from analisador_semantico import analisar_codigo

res = analisar_codigo(codigo)          # scanner + parser + semântica
if res.rejeitado_antes_da_semantica:
    print(res.erros_lexicos, res.erros_sintaticos)
else:
    for d in res.semantico.diagnosticos:
        print(d)                       # "SEMxxx — linha L, coluna C: mensagem"
```

**Códigos de saída:** `0` programa aceito; `1` programa rejeitado por erros semânticos; `2` entrada rejeitada pelo scanner/parser, ou uso incorreto (arquivo não informado).

## Saída de exemplo do analisador semântico

Para o código:

```c
int main() {
    int x = 1.5;
    y = 2;
    return 0;
}
```

a saída é:

```
SEM003 — linha 2, coluna 13: Não é possível atribuir float a int sem conversão permitida (destino “x”; expressão “1.5”).
SEM001 — linha 3, coluna 5: Identificador “y” não declarado neste escopo.
Análise semântica concluída: 2 erros; programa rejeitado.
```

Para um programa sem erros semânticos:

```
Análise semântica concluída: 0 erros; programa aceito.
```

---

# Gerador de IR

O gerador de IR (`gerador_ir.py` / `gerador_ir.c`) implementa a etapa de **geração de código intermediário**, logo após o analisador semântico. Ele percorre a **AST anotada** (campo `sem`, produzido pela análise semântica) e emite uma **IR linear de três endereços**, em texto, no estilo de código de três endereços com rótulos e saltos.

Como nas etapas anteriores, existe em Python (classe `GeradorIR`) e em C (estrutura `GeradorIR` e funções `gi_*`), com o mesmo comportamento e a mesma saída. A versão em C inclui `analisador_semantico.c` (que inclui `parser.c` e `scanner.c`).

## Como funciona o gerador de IR

- O gerador é um **visitor** sobre a AST: para cada tipo de nó existe um método `visit_<Nó>` (`visit_Function`, `visit_If`, `visit_Binary` etc.). Um nó sem visitor gera `NotImplementedError`.
- **Comandos** emitem linhas diretamente; **expressões** devolvem o *operando* onde o resultado fica (um literal, um nome de variável ou um temporário `tN`).
- Em expressões binárias, o operando da **esquerda é avaliado antes** do da direita. Em chamadas, os argumentos são avaliados em ordem, e o temporário do resultado só é criado **depois** dos `param`.
- `&&` e `||` viram as operações `and` e `or` e **não** usam curto-circuito: os dois operandos são sempre avaliados antes da operação.
- Declarações de variáveis globais (fora de funções) emitem `x = valor` no recuo base, fora de qualquer `func`. Declarações de vetor não geram instrução.
- Variáveis locais sem inicializador não geram instrução.

## Formato da IR

Cada função é delimitada por `func nome(params)` e `endfunc`. As instruções têm recuo de 2 espaços; os rótulos ficam na coluna 0.

| Forma | Significado |
|---|---|
| `func nome(a, b)` ... `endfunc` | Início e fim de função (sem parênteses se não houver parâmetros). |
| `x = valor` | Cópia/atribuição. `valor` pode ser literal, variável ou temporário. |
| `t1 = a op b` | Operação binária: `+ - * / % < <= > >= == !=`, `and`, `or`. |
| `t1 = -a` / `t1 = not a` | Operações unárias. |
| `t1 = idx * 1` | Cálculo do deslocamento de um índice de vetor (fator de escala `ESCALA = 1`). |
| `t2 = load v[t1]` | Leitura de elemento de vetor. |
| `store v[t1], valor` | Escrita em elemento de vetor. |
| `ifFalse c goto L` | Salto condicional quando `c` é falso. |
| `goto L` / `L:` | Salto incondicional e definição de rótulo. |
| `param x` | Empilha um argumento. |
| `call f, n` / `t1 = call f, n` | Chamada com `n` argumentos (com ou sem resultado). |
| `return` / `return x` | Retorno de função. |
| `print x` | Impressão. |
| `read x` / `read v[t1]` | Leitura para variável ou para elemento de vetor. |

Literais: `true`/`false` para `bool`, strings em JSON entre aspas duplas, `char` entre apóstrofos (com `'` interno escapado), números como no código-fonte.

## Convenções de nomes e rótulos

- **Temporários** são `t1`, `t2`, ... e a numeração **reinicia a cada função**.
- **Rótulos** são descritivos e únicos por função: `L_test`, `L_end`, `L_else`, `L_end_if`, `L_inc`, `L_rec`. Em caso de repetição, recebem sufixo numérico (`L_end_2`, `L_end_3`...).
- **Sombreamento léxico:** quando o mesmo nome é declarado mais de uma vez na mesma função (símbolos diferentes), os símbolos são renomeados. Com **dois** símbolos, `x#outer` e `x#inner`; com três ou mais, `x#1`, `x#2`, `x#3`... O símbolo de cada nó vem de `sem.simbolo`.

| Construção | IR gerada |
|---|---|
| `if` sem `else` | `ifFalse c goto L_end_if` · então · `L_end_if:` |
| `if` com `else` | `ifFalse c goto L_else` · então · `goto L_end` · `L_else:` · senão · `L_end:` |
| `while` | `L_test:` · condição · `ifFalse c goto L_end` · corpo · `goto L_test` · `L_end:` |
| `for` | init · `L_test:` · condição · `ifFalse c goto L_end` · corpo · (`L_inc:`) · incremento · `goto L_test` · `L_end:` |
| `break` | `goto` para o `L_end` do laço mais interno. |
| `continue` | `goto` para o `L_test` (em `while`) ou para `L_inc` (em `for`). `L_inc:` só é emitido se algum `continue` foi usado. |
| Chamada `void` como comando | `call f, n` sem temporário. |
| Atribuição encadeada | A atribuição devolve o nome do destino, que é reaproveitado como operando. |

### Padrões especiais

- **Guarda com retorno:** um `if` sem `else` cujo ramo "então" termina em `return` e que é seguido de outros comandos é gerado como `ifFalse c goto L_rec` · então · `goto L_end` · `L_rec:` · *comandos restantes* · `L_end:`. Esses dois rótulos são "fixos" (não ativam a observação "rótulos podem variar").
- **Inicialização antecipada do `for`:** se uma variável é declarada sem inicializador e o próximo `for` que a usa começa com `x = literal`, essa atribuição é emitida logo na posição da declaração e não é repetida no `for`.

## Sinalizadores de recursos usados

O gerador registra o que foi utilizado, e o `minic` usa isso para montar o cabeçalho da saída:

| Atributo | Ativado quando |
|---|---|
| `usou_rotulos` | Algum rótulo não fixo foi criado (`if`, `while`, `for`). |
| `usou_vetor` | Houve cálculo de endereço de vetor (leitura, escrita ou `read`). |
| `usou_logico` | Houve `and`, `or` ou `not`. |
| `usou_sombra` | Algum nome foi renomeado por sombreamento. |

## Referência das funções do gerador

| Função (Python) | Descrição |
|---|---|
| `gerar(ast)` | API principal: zera o estado, visita a AST e devolve a IR como texto (uma linha por instrução, terminada em `\n`). |
| `visitar(no)` | Despacha para `visit_<tipo do nó>`. |
| `emitir(texto)` / `rotulo_def(rotulo)` | Acrescenta uma instrução (com recuo) ou um rótulo (coluna 0). |
| `novo_temp()` / `novo_rotulo(base, fixo)` | Criam um temporário `tN` ou um rótulo único. |
| `_preparar_nomes(func)` / `_nome(no)` | Detectam símbolos sombreados e devolvem o nome final de cada identificador. |
| `_visitar_itens(itens)` | Visita os itens de um bloco em ordem, aplicando a antecipação do `for` e o padrão de guarda. |
| `_eh_guarda_com_retorno` / `_gerar_guarda` | Reconhecem e geram o padrão de guarda com retorno. |
| `_antecipar_init_for(decl, resto)` | Implementa a inicialização antecipada do `for`. |
| `_endereco(idx_no)` | Emite o cálculo `t = idx * ESCALA` e devolve `(base, t)`. |
| `_gerar_chamada(no, com_resultado)` | Emite `param`s e `call`, com ou sem temporário de resultado. |
| `visit_*` | Um visitor por nó da AST (`Program`, `Function`, `Block`, `VarDecl`, `If`, `While`, `For`, `Return`, `Break`, `Continue`, `Print`, `Read`, `ExprStmt`, `Assign`, `Binary`, `Unary`, `Call`, `Index`, `Id`, `Lit`). |

> Na versão em C, as funções equivalentes têm o prefixo `gi_` (`gi_visit_for`, `gi_novo_rotulo`, `gi_gerar` etc.).

## Exemplo de uso do gerador de IR

O gerador é normalmente chamado pelo `minic`, mas também pode ser usado diretamente em Python:

```python
from analisador_semantico import analisar_codigo
from gerador_ir import GeradorIR

res = analisar_codigo(codigo)
if res.sucesso:
    g = GeradorIR()
    ir = g.gerar(res.ast)
    print(ir)
```

Para o código:

```c
int main() {
    int a = 2 + 3;
    int b = a * 4;
    print(b);
    return 0;
}
```

a IR gerada é:

```
func main
  t1 = 2 + 3
  a = t1
  t2 = a * 4
  b = t2
  print b
  return 0
endfunc
```

---

# Otimizador de IR

O otimizador (`otimizador_ir.py` / `otimizador_ir.c`) recebe a **IR em texto** produzida pelo gerador e devolve uma IR equivalente com menos instruções. Ele trabalha apenas sobre o texto da IR (não depende da AST) e oferece dois modos:

- **`otimizar(ir)` (modo `-O2`)**: aplica **todas** as otimizações, repetidamente, a cada função, e devolve a IR otimizada.
- **`otimizar_alvo(ir)` (modo `-O1`)**: identifica **uma** oportunidade de otimização (a de maior prioridade) e devolve um relatório com o alvo, a IR de referência (sem otimizar) e a IR otimizada.

A versão em C (funções `ot_*`) usa inteiros de 128 bits (`__int128`, portanto GCC ou Clang) no lugar dos inteiros ilimitados do Python e uma arena de memória liberada ao fim de cada operação pública; o comportamento é o mesmo.

## Como funciona o otimizador

1. **Segmentação** (`_segmentar`): o texto é dividido em *partes*: funções (`func ... endfunc`) e linhas soltas (inicializações globais). Cada linha de função é convertida em uma **tupla** de instrução (`copy`, `bin`, `un`, `load`, `store`, `ifFalse`, `goto`, `label`, `print`, `param`, `call`, `read`, `readidx`, `ret`).
2. **Variáveis globais** são os nomes atribuídos fora de funções, mais os nomes não temporários usados em **mais de uma função** (parâmetros excluídos). Globais nunca são removidas por código morto, e seus valores conhecidos são esquecidos após cada `call`.
3. Cada função é otimizada de forma **independente**, sobre o seu **grafo de fluxo de controle** (CFG), construído por `construir_cfg` (blocos básicos e sucessores).
4. **Falha segura:** se uma linha não pode ser interpretada, se falta um rótulo ou se uma análise iterativa não converge (`KeyError`/`RuntimeError`), a função afetada é devolvida **sem alteração**.
5. No fim, `compactar_temps` renumera os temporários sem buracos (`t1`, `t2`, ...).

## Transformações implementadas

| Transformação | Função | O que faz |
|---|---|---|
| Propagação de constantes e cópias | `propagar`, `passo` | Análise de fluxo de dados sobre o CFG (interseção nos pontos de junção) que substitui operandos por constantes ou cópias conhecidas. |
| Dobra de constantes | `fold_bin`, `simp_un` | Avalia operações entre literais `int` e `bool` (aritmética, comparações, `and`, `or`, `not`). |
| Simplificação algébrica | `alg_bin` | `x+0`, `x-0`, `x*1`, `x/1`, `and`/`or` com `true`/`false`. |
| Dobra de condições | `passo` | `ifFalse true` é removido; `ifFalse false` vira `goto`. |
| Subexpressão comum local | `cse_local` | Reaproveita resultados de `bin`/`un` iguais dentro de um bloco (operações comutativas são normalizadas). |
| Fluxo de controle | `limpar_fluxo` | Remove blocos inalcançáveis, rótulos sem uso e saltos para a instrução seguinte. |
| Code motion (LICM) | `mover_invariantes` | Move para antes do laço temporários invariantes (definidos uma única vez, com operandos não modificados no laço). |
| Redução de força | `reduzir_forca` | Troca `v * c` por uma variável de atualização incremental quando `v` é variável de indução com valor inicial literal. |
| Código morto | `eliminar_mortos` | Usa análise de vivacidade (`_vivas_entrada`) para remover definições sem uso; `call` sem uso do resultado perde só o destino. |
| Fusão de temporários | `fundir_temporarios` | Transforma `t = a + b` seguido de `x = t` em `x = a + b` quando `t` morre ali. |

### Regras de segurança

- A dobra de constantes só ocorre para **`int` dentro de 32 bits** (`-2^31` a `2^31-1`); resultados que estouram **não** são dobrados. Divisão e módulo seguem **truncamento em direção a zero** e nunca são dobrados com divisor zero. Valores `float` não são dobrados.
- Divisões e módulos cujo divisor não é uma constante diferente de zero (`_div_perigosa`) **não** são removidos nem movidos para fora de laços.
- Code motion e redução de força só são aplicadas em laços de **entrada única** (nenhum salto externo cai no meio do laço), e LICM não move expressões que dependem de globais quando o laço contém `call`.
- Instruções com efeito observável (`print`, `read`, `param`, `call`, `store`, `return`) nunca são removidas.

## Modo `-O2`: otimização completa

`otimizar_funcao` repete até **30 vezes** (ou até a IR parar de mudar) a sequência:

```
propagar → cse_local → limpar_fluxo → mover_invariantes → eliminar_mortos
```

e, ao final, aplica `fundir_temporarios` e `limpar_fluxo`. A redução de força **não** faz parte do `-O2`; ela só aparece no modo `-O1`.

## Modo `-O1`: um alvo por vez

`otimizar_alvo` testa os alvos abaixo **em ordem de prioridade** e aplica apenas o primeiro que for detectado em alguma função. Cada alvo usa uma configuração restrita das transformações, de modo que o resultado ilustre somente aquela otimização.

| Prioridade | Classe | Alvo informado no relatório |
|---|---|---|
| 1 | `inalcancavel` | eliminação de código morto/inalcançável (ativa o aviso opcional) |
| 2 | `licm` | code motion de expressão loop-invariant |
| 3 | `forca` | redução de força / atualização incremental de expressão relacionada à indução |
| 4 | `cse` | eliminação de subexpressão comum local |
| 5 | `algebra` | simplificação algébrica e propagação de cópias |
| 6 | `copias` | eliminação de cópias |
| 7 | `morta` | eliminação de atribuição morta |
| 8 | `constantes` | dobra de condição constante, propagação de constantes/dobra algébrica ou propagação global de constantes (o texto varia conforme o caso) |
| 9 | `geral` | otimizações gerais (constantes, cópias, subexpressões, código morto) |

Se nenhum alvo é detectado, o relatório traz `nenhuma oportunidade de otimização encontrada`.

### Relatório devolvido

`otimizar_alvo` devolve um dicionário:

| Chave | Conteúdo |
|---|---|
| `alvo` | Descrição textual da otimização escolhida. |
| `classe` | Identificador da classe (tabela acima, ou `nenhuma`). |
| `ref` | IR de referência (sem otimizar). Para `licm` e `forca`, é cortada no salto de volta do laço. |
| `titulo` | Título da IR otimizada (`IR otimizada esperada (-O1), equivalente:` ou, no caso de `forca`, `IR otimizada possível (-O1), equivalente:`). |
| `opt` | IR otimizada. |
| `extras` | Linhas adicionais (`Observação:`, `Critério:`, `Verificar:`). |
| `aviso` | `True` para `inalcancavel` (aviso opcional de código inalcançável). |

Detalhes de apresentação: quando o programa tem uma única função `main`, apenas o corpo é exibido (sem `func`/`endfunc`) e o `return 0` final é omitido da referência. No alvo `inalcancavel`, chamadas com zero argumentos aparecem precedidas de um `param` vazio na IR otimizada.

## Referência das funções do otimizador

| Função (Python) | Descrição |
|---|---|
| `parse(txt)` / `fmt(ins)` | Convertem uma linha de IR em tupla e vice-versa. |
| `usos(ins)` / `defs(ins)` | Operandos (não literais) lidos e nome definido por uma instrução. |
| `eh_lit(x)` | Indica se um operando é literal (número, `true`/`false`, string ou char). |
| `construir_cfg(instrs)` | Divide em blocos básicos e calcula os sucessores. |
| `propagar` / `passo` / `Cfg` | Propagação de constantes/cópias; `Cfg` liga/desliga constantes, álgebra e cópias. |
| `fold_bin` / `alg_bin` / `simp_bin` / `simp_un` | Dobra de constantes e simplificações algébricas. |
| `cse_local` | Eliminação de subexpressão comum dentro de blocos. |
| `limpar_fluxo` | Remove código inalcançável, rótulos e saltos redundantes. |
| `mover_invariantes` | Uma passada de code motion (a mais interna primeiro). |
| `reduzir_forca` | Uma passada de redução de força por variável de indução. |
| `eliminar_mortos` / `fundir_temporarios` | Código morto e fusão de temporários, ambos por vivacidade. |
| `compactar_temps` | Renumera os temporários. |
| `otimizar_funcao(instrs, glob)` | Pipeline do `-O2` para uma função. |
| `otimizar(ir_texto)` | API do modo `-O2`: texto → texto. |
| `otimizar_alvo(ir_texto)` | API do modo `-O1`: texto → relatório (dicionário). |

> Na versão em C, as funções equivalentes têm o prefixo `ot_` (`ot_propagar`, `ot_otimizar`, `ot_otimizar_alvo` etc.).

## Exemplo de uso do otimizador

```python
from otimizador_ir import otimizar, otimizar_alvo

ir = """func main
  x = 3
  y = 4
  s = 0
  i = 0
L_test:
  t1 = i < 10
  ifFalse t1 goto L_end
  t2 = x * y
  t3 = s + t2
  s = t3
  t4 = i + 1
  i = t4
  goto L_test
L_end:
  print s
  return 0
endfunc
"""

print(otimizar(ir))          # -O2: todas as otimizações
rel = otimizar_alvo(ir)      # -O1: um alvo
print(rel["alvo"], rel["opt"], sep="\n")
```

Com `-O2`, a propagação de constantes dobra `x * y` para `12` e remove as atribuições mortas:

```
func main
  s = 0
  i = 0
L_test:
  t1 = i < 10
  ifFalse t1 goto L_end
  s = s + 12
  i = i + 1
  goto L_test
L_end:
  print s
  return 0
endfunc
```

Com `-O1`, o alvo detectado é `code motion de expressão loop-invariant`, e a IR otimizada traz `t2 = x * y` movido para antes de `L_test:`.

---

# Driver `minic`

O `minic` (`minic.py` / `minic.c`) é o **programa principal** que une todas as etapas:

```
scanner → parser → analisador semântico → gerador de IR → otimizador de IR
```

Ele lê um arquivo-fonte, executa o pipeline e imprime na saída padrão um relatório textual: ou **`COMPILAR COM SUCESSO`**, seguido da IR, ou **`REJEITAR`**, com a categoria do erro e o critério.

A versão em Python importa `scanner`, `analisador_semantico`, `gerador_ir` e `otimizador_ir`. A versão em C inclui `gerador_ir.c` e `otimizador_ir.c` (e, por eles, o analisador semântico, o parser e o scanner), então **todos os `.c` devem estar na mesma pasta**.

## Uso

```bash
python3 minic.py caminho/para/arquivo.c [-O0 | -O1 | -O2]
```

Ou, usando a versão em C:

```bash
gcc -o minic minic.c -lm
./minic caminho/para/arquivo.c [-O0 | -O1 | -O2]
```

### Níveis de otimização

| Nível | Efeito |
|---|---|
| `-O0` (padrão) | IR sem otimização, direto do gerador. |
| `-O1` | Relatório de **um alvo** de otimização (`otimizar_alvo`). |
| `-O2` | IR com **todas** as otimizações (`otimizar`). |

O nível é escolhido, por ordem: pela opção `-O0`/`-O1`/`-O2` na linha de comando; pela variável de ambiente `MINIC_OPT` (`1`/`O1` ou `2`/`O2`); ou, se nenhuma das duas foi informada, pelo **nome do arquivo**: um arquivo cujo nome-base é `O` seguido de dígitos (por exemplo `O3.c`) é compilado com `-O1`.

### Variáveis de ambiente

| Variável | Efeito |
|---|---|
| `MINIC_OPT` | Define o nível de otimização (`1`, `O1`, `2`, `O2`). Quando presente, desativa a escolha pelo nome do arquivo. |
| `MINIC_DEBUG` | Se definida, imprime em `stderr` os erros léxicos, sintáticos e semânticos completos. |

## Formato da saída

**Sucesso em `-O0`:**

```
Resultado: COMPILAR COM SUCESSO[ (assumindo a política MINIC de sombreamento léxico)]
Modo de referência: IR sem otimização (-O0)
IR canônica esperada[ complemento]:
<IR>
[Observações]
```

O `complemento` depende dos sinalizadores do gerador, nesta ordem de prioridade: `(LOAD/STORE equivalentes aceitos)` se há vetor; `(curto-circuito ou operação lógica explícita)` se há operação lógica; `(rótulos podem variar)` se há rótulos não fixos. As observações finais são `Observação: o fator de escala do endereço...` (vetores) e `Observação: nomes temporários/endereços de símbolos...` (sombreamento).

**Sucesso em `-O1`:**

```
Resultado: COMPILAR COM SUCESSO[ (aviso opcional de código inalcançável)]
Alvo: <alvo>
IR de referência (-O0):
<ref>
<título>
<IR otimizada>
<linhas extras>
```

**Sucesso em `-O2`:**

```
Resultado: COMPILAR COM SUCESSO
Modo de referência: IR otimizada (-O2)
IR otimizada (todas as otimizações):
<IR>
```

Para o código de exemplo do gerador (`a = 2 + 3; b = a * 4; print(b)`), a saída em `-O2` é:

```
Resultado: COMPILAR COM SUCESSO
Modo de referência: IR otimizada (-O2)
IR otimizada (todas as otimizações):
func main
  print 20
  return 0
endfunc
```

**Rejeição:**

```
Resultado: REJEITAR
Categoria esperada: erro <léxico|sintático|semântico> (código de saída N)
Critério: <descrição>
```

O critério é montado a partir do **primeiro** erro da etapa que rejeitou a entrada:

- **Léxico:** `diagnóstico de símbolo “@” não reconhecido.` para um caractere isolado, ou `diagnóstico léxico: <código> “<lexema>”.` nos demais casos.
- **Sintático:** `diagnóstico de “;” esperado.` (ou `... após declaração local.` quando a linha anterior é uma declaração); nos demais casos, a própria mensagem do parser.
- **Semântico:** a mensagem do primeiro diagnóstico é classificada por palavras-chave (acentos e maiúsculas são ignorados): `break`/`continue` fora de laço, índice não `int`, retorno incompatível (com tipos e nome da função), número de argumentos, declaração duplicada, identificador não declarado, operando `bool` em operador aritmético, conversão implícita `bool -> int`. Se nada casa, o texto bruto da mensagem é usado.

## Códigos de saída

| Código | Significado |
|---|---|
| `0` | Compilação com sucesso. |
| `1` | Erro de E/S (arquivo não informado ou ilegível) ou limite de aninhamento excedido. |
| `2` | Erro léxico. |
| `3` | Erro sintático. |
| `4` | Erro semântico. |

> Esses códigos valem para o `minic`. O `analisador_semantico` executado isoladamente mantém os códigos próprios descritos na seção do analisador semântico.

## Funções do driver

| Função (Python) | Descrição |
|---|---|
| `compilar(codigo, nivel)` | Executa o pipeline e devolve `(código de saída, texto)`. Captura o limite de aninhamento (`RecursionError`/`LimiteAninhamento`). |
| `montar_saida(g, ir)` / `montar_saida_o1(r)` / `montar_saida_o2(ir)` | Montam o texto de sucesso de cada nível. |
| `montar_rejeicao(categoria, codigo, criterio)` | Monta o texto de rejeição. |
| `_criterio_lexico` / `_criterio_sintatico` / `_criterio_semantico` | Derivam o critério a partir do primeiro erro. |
| `main()` | Interpreta `-O0/-O1/-O2` e `MINIC_OPT`, lê o arquivo, chama `compilar` e encerra com o código de saída. |

> Na versão em C, as funções equivalentes têm o prefixo `mc_` (`mc_compilar`, `mc_montar_rejeicao` etc.).
