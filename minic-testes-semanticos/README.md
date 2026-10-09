# Suíte de testes semânticos MINIC

Contém 20 fontes `.c`: 10 casos de aceitação e 10 de rejeição semântica, além do gabarito completo em `GABARITO.md`.

## Uso

Execute cada fonte pelo parser/analisador do projeto. Todos os arquivos foram escritos para a gramática Bison/Flex fornecida. A gramática constrói AST, mas o pacote de projeto examinado não inclui ainda a implementação semântica; por isso, consulte `GABARITO.md` como especificação dos resultados e mensagens canônicas, não como saída já emitida pelo parser.

## Premissas

As regras semânticas seguem a apostila da Aula 15: escopo léxico com sombreamento, compatibilidade exata exceto promoção `int` para `float`, verificação de vetores, chamadas, retornos, condições booleanas e destinos atribuíveis. As fontes evitam comentários e tokens não reconhecidos pelo lexer fornecido.
