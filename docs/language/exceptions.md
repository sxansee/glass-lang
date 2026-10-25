# Исключения: `try`, `catch`, `finally`, `throw`

`throw` бросает исключение — объект типа `Exception` или его потомка. `try { ... } catch e: Тип { ... }` перехватывает: выполняется ближайший подходящий `catch`. У `Exception` есть поле `message`.

```glass run
fn divide(a: int, b: int) -> int {
    if b == 0 {
        throw Exception("делить на ноль нельзя")
    }
    return a / b
}

fn main() {
    try {
        print(divide(10, 2))
        print(divide(1, 0))
        print("сюда не дойдём")
    } catch e: Exception {
        print("ошибка:", e.message)
    }
}
```

```output
5
ошибка: делить на ноль нельзя
```

Слова `catch` и `finally`, как и `else`, можно писать и на одной строке с `}`, и на следующей.

## Свои исключения и `finally`

Своё исключение — это `mold`, наследующий `Exception`. Блок `finally` выполняется всегда: и после успеха, и после ошибки.

```glass run
mold NotFound : Exception {
    NotFound(msg: string) {
        super(msg)
    }
}

fn find(id: int) -> string {
    if id != 1 {
        throw NotFound("нет пользователя " + id)
    }
    return "Ада"
}

fn main() {
    try {
        print(find(1))
        print(find(2))
        print("недостижимо")
    } catch e: NotFound {
        print("не найден:", e.message)
    } finally {
        print("finally выполнился")
    }
}
```

```output
Ада
не найден: нет пользователя 2
finally выполнился
```

## Порядок `catch`, перехват по предку, проброс дальше

`catch e: Тип` ловит и потомков типа: `catch e: Exception` поймает любое исключение. Из `catch` можно бросить исключение дальше — его перехватит внешний `try`.

```glass run
mold AppError : Exception {
    AppError(msg: string) {
        super(msg)
    }
}

fn risky() {
    throw AppError("внутри")
}

fn main() {
    try {
        try {
            risky()
        } catch e: AppError {
            print("перехватил:", e.message)
            throw Exception("проброшено дальше")
        }
    } catch e: Exception {
        print("снаружи:", e.message)
    }

    try {
        risky()
    } catch e: Exception {
        print("по базовому типу:", e.message)
    }

    try {
        print("всё хорошо")
    } finally {
        print("finally выполнился и без ошибки")
    }
}
```

```output
перехватил: внутри
снаружи: проброшено дальше
по базовому типу: внутри
всё хорошо
finally выполнился и без ошибки
```

## Непойманное исключение

Если ни один `catch` не подошёл, программа останавливается и сообщает об исключении:

```glass run
fn main() {
    print("до")
    throw Exception("беда")
}
```

```output
до
Ошибка выполнения: необработанное исключение (Exception): беда
```

## Ошибки выполнения — не исключения

Деление на ноль, выход за границы массива, неверное приведение объекта, `int("abc")` — это **ошибки выполнения самой VM**. Это не объекты `Exception`, и `try` / `catch` их не ловит: программа останавливается с сообщением и номером строки.

```glass run
fn main() {
    try {
        let zero = 0
        print(1 / zero)
    } catch e: Exception {
        print("поймано")
    }
    print("сюда не дойдём")
}
```

```output
Ошибка выполнения: main:4: деление на ноль
```

## Что пока не поддерживается

`return`, `break` и `continue` **внутри** `try`, у которого есть `finally`, пока не поддерживаются — компилятор честно отказывает, вместо того чтобы молча пропустить `finally`:

```glass error
fn first() -> int {
    try {
        return 1
    } finally {
        print("уборка")
    }
}

fn main() {
    print(first())
}
```

```output
Ошибка компиляции в байткод: codegen (3): 'return' внутри try/catch с finally пока не поддерживается — finally в этом случае гарантированно не выполнился бы
```

Выход — вынести `return` за пределы `try` (запомнить результат в переменной). Это сознательное упрощение, оно описано в [README](../../README.md).

## Что дальше

[Объекты](objects.md) · [Интроспекция](introspection.md) · [Справочник](../reference/language-reference.md)
