# Управление потоком

## `if` / `else if` / `else`

Условие не берётся в скобки; тело — всегда блок в фигурных скобках. `else` можно писать и на одной строке с `}`, и на следующей.

```glass run
fn describe(x: int) -> string {
    if x > 10 {
        return "много"
    }
    else if x > 5 {
        return "средне"
    } else {
        return "мало"
    }
}

fn main() {
    print(describe(20), describe(7), describe(1))
}
```

```output
много средне мало
```

## `while`

```glass run
fn main() {
    let i = 1
    while i <= 3 {
        print("шаг", i)
        i += 1
    }
}
```

```output
шаг 1
шаг 2
шаг 3
```

`break` выходит из цикла, `continue` переходит к следующей итерации:

```glass run
fn main() {
    let i = 0
    while true {
        i += 1
        if i == 2 { continue }
        if i > 4 { break }
        print("i =", i)
    }
}
```

```output
i = 1
i = 3
i = 4
```

## `for`

`for` перебирает диапазон, массив или строку. Диапазон `от..до` не включает правую границу; функция `range` добавляет шаг и счёт назад.

```glass run
fn main() {
    for k in 1..4 { print(k) }
    print("--")
    for k in range(3) { print(k) }
    print("--")
    for k in range(2, 10, 3) { print(k) }
    print("--")
    for k in range(5, 0, -2) { print(k) }
}
```

```output
1
2
3
--
0
1
2
--
2
5
8
--
5
3
1
```

- `range(n)` — числа от 0 до `n - 1`;
- `range(a, b)` — от `a` до `b - 1`;
- `range(a, b, шаг)` — с шагом, отрицательный шаг считает назад (шаг не может быть нулём).

В цикле `for` массив для `range` не создаётся — числа выдаются по одному.

Перебор массива и строки:

```glass run
fn main() {
    for name in ["Ада", "Грейс"] {
        print("привет,", name)
    }
    for ch in "abc" {
        print(ch)
    }
}
```

```output
привет, Ада
привет, Грейс
a
b
c
```

## Вложенные циклы

```glass run
fn main() {
    for a in 1..3 {
        for b in 1..4 {
            print(a, "x", b, "=", a * b)
        }
    }
}
```

```output
1 x 1 = 1
1 x 2 = 2
1 x 3 = 3
2 x 1 = 2
2 x 2 = 4
2 x 3 = 6
```

## Что дальше

[Функции](functions.md) · [Массивы](arrays.md) · [Строки](strings.md) · [Справочник](../reference/language-reference.md)
