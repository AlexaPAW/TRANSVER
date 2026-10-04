
## Сборка

```bash
g++ -std=c++17 -O2 -Wall -Wextra -pedantic src/main.cpp -o funny_lexer
```

## Запуск

Построить автоматы и экспортировать их:

```bash
./funny_lexer --dump --spec tokens.regex --out out

./funny_lexer --dump
```

Прогнать тесты:

```bash
./funny_lexer --run-tests --spec tokens.regex --tests tests/tests.tsv --out out

./funny_lexer --run-tests
```

Проверить отдельную строку:

```bash
./funny_lexer --scan 'function f(int x) returns y: int { assert x >= 0; }' --spec tokens.regex --out out

./funny_lexer --scan 'function f(int x) returns y: int { assert x >= 0; }'
```

Код возврата `0` — успех, `2` — ошибка лексирования/CLI.

## Формат `tokens.regex`

NAME<TAB>FLAGS<TAB>PRIORITY<TAB>REGEX

Флаги:

- `S` — токен пропускается;
- `E` — лексическая ошибка;
- пусто — обычный принимающий токен.

Число `PRIORITY` используется при совпадении нескольких правил одинаковой длины: меньшее значение имеет больший приоритет

## Формат тестов

INPUT<TAB>EXPECTED

`<EMPTY>` означает, что после удаления `WS`/`COMMENT` токенов ничего не осталось, `ERROR` означает лексическую ошибку

## Файлы результата

После запуска в `out/` создаются:

- `dfa.json` — минимизированный ДКА
- `dfa.csv` — таблица переходов
- `dfa_table.hpp` — готовая таблица для подключения в лексер
- `test_results.tsv` — статусы тестов (при `--run-tests`)

