## Сборка

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude src/*.cpp  -o funny_parser
```

## Запуск

Файл:

```bash
./funny_parser examples/complete.funny
```

Строка:

```bash
./funny_parser --string 'function f(x: int) returns y: int { y = x + 1; }'
```

`--compact` выводит JSON в одну строку

```bash
./funny_parser examples/complete.funny --compact
```

При успешном запуске из файла или через `--string` тот же JSON сохраняется в `output/output.json`. Файл создаётся автоматически и перезаписывается при каждом успешном запуске. При лексической/синтаксической ошибке файл не обновляется.

Коды возврата: `0` — валидная программа, `1` — лексическая/синтаксическая ошибка, `2` — ошибка CLI/I/O.

## Тесты

```bash
bash tests/run_tests.sh ./funny_parser
```

## Интеграция с HW1

`include/dfa_table.hpp` взят из HW1 без изменения.

## AST и JSON

На успешном разборе программа выдаёт JSON-дерево AST. Объект всегда имеет вид `{"kind":"Program", "declarations":[...]}`. `kind` определяет тип узла. `pos` указывает начало соответствующей синтаксической конструкции во входном тексте.

Пример:

```json
{
  "kind": "FunctionDecl",
  "name": "f",
  "params": [{"kind":"VariableDef", "name":"x", "type":"int"}],
  "requires": {"kind":"BoolLiteral", "value":true},
  "returns": [{"kind":"VariableDef", "name":"y", "type":"int"}],
  "ensures": {"kind":"BoolLiteral", "value":false},
  "uses": [],
  "body": {
    "kind": "Block",
    "statements": [
      {
        "kind": "Assignment",
        "target": "y",
        "indices": [],
        "value": {
          "kind":"BinaryExpr", "op":"+",
          "left":{"kind":"Variable","name":"x"},
          "right":{"kind":"IntLiteral","value":1}
        }
      }
    ]
  }
}
```

Основные узлы:

- `Program`, `FunctionDecl`, `FormulaDecl`;
- `VariableDef`, `LocalVarDef`;
- `IntLiteral`, `Variable`, `ArrayAccess`, `UnaryExpr`, `BinaryExpr`, `CallExpr`;
- `BoolLiteral`, `Comparison`, `UnaryPredicate`, `BinaryPredicate`, `FormulaRef`, `Quantifier`;
- `Block`, `Assignment`, `If`, `While`, `Assert`, `Assume`.

Дополнительная справочная информация находится в funny_grammar.ebnf

## LL(1)-замечание

При построении `Parser` заранее за один проход вычисляются пары соответствующих `(` / `)`. За `)` проверяется следующий токен: если это оператор сравнения, содержимое скобок трактуется как выражение — левый операнд comparison; иначе внутри разбирается `condition`/`predicate`. Поэтому на каждой неоднозначной позиции решение принимается детерминированно за `O(1)`, а построение таблицы пар скобок занимает `O(n)`.
