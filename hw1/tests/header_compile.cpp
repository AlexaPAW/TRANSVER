#include "../out/dfa_table.hpp"
int main() {
    static_assert(funny_dfa::kAlphabetSize == 128);
    return funny_dfa::kTransition[funny_dfa::kStartState]['a'] == funny_dfa::kTrapState ? 1 : 0;
}

// этот файл проверяет:
//   1) header компилируется
//   2) размер алфавита = ожидаемым 128 ASCII-кодам
//   3) таблица переходов доступна как C++-массив
//   4) для конкретного перехода можно получить состояние