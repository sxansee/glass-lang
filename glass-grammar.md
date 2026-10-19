# Glass — формальная грамматика (EBNF)

**Версия 2.** Синтаксис полностью переработан и больше не похож на C# — семантика (статическая типизация, ООП, единичное наследование + facet'ы) осталась той же, потому что это проверенная модель, а не потому что язык её копирует. Подробное обоснование — раздел «Сравнение с C#/Java» в glass-differentiation.md.

**Соглашения нотации:**
- `::=` — «определяется как»
- `|` — альтернатива
- `[ x ]` — необязательно (0 или 1 раз)
- `{ x }` — повторение (0 или более раз)
- `"текст"` — терминал (буквальный текст)
- Имя с большой буквы — нетерминал

---

## 1. Лексическая грамматика

```
letter          ::= "a".."z" | "A".."Z" | "_"
digit           ::= "0".."9"
identifier      ::= letter { letter | digit }

int_literal     ::= digit { digit }
double_literal  ::= digit { digit } "." digit { digit } [ ("e"|"E") ["+"|"-"] digit { digit } ]
bool_literal    ::= "true" | "false"
string_literal  ::= '"' { string_char } '"'
string_char     ::= любой символ кроме '"' и перевода строки | "\n" | "\t" | "\"" | "\\"
null_literal    ::= "null"

line_comment    ::= "//" { любой символ кроме перевода строки }
block_comment   ::= "/*" { любой символ } "*/"
whitespace      ::= " " | "\t" | "\r"
```

**Ключевые слова:**

```
address bool break catch continue double else facet false finally
fn for gc heap if in int layout let mold null override private
protected public return static string super this throw true try
vtable virtual while
```

**Автоматическая вставка `;` (ASI).** Точки с запятой необязательны. Лексер сам вставляет неявный `;` перед переводом строки (и перед EOF), если последний токен — идентификатор; литерал (`true`, `false`, `null`, число, строка); ключевое слово `this`, `super`, `return`, `break` или `continue`; примитивный тип (`int`, `double`, `bool`, `string`); одно из слов интроспекции (`layout`, `vtable`, `address`, `heap`, `gc`); либо один из символов `) ] }`. В остальных случаях (последний токен — оператор, `(`, `[`, `,`, `.`, `:`) перенос строки не завершает выражение. Правило соответствует автоматической вставке точек с запятой в Go, с одним добавлением: после `}` перенос строки `;` **не** вставляет, если следующее слово — `else`, `catch` или `finally`. Поэтому привычный стиль

```
if x > 3 {
    ...
}
else {
    ...
}
```

разбирается как один оператор `if`/`else` (то же для `try`/`catch`/`finally`). Это безопасно: ни один оператор не может начинаться с этих слов, а тело `if`/`try` — всегда блок в фигурных скобках. Слово должно быть целым: идентификатор `elsewhere` обычным образом завершает оператор.

Практические следствия, о которых нужно помнить (оба — как в Go, не недосмотр):
- значение после `return` должно начинаться на той же строке, что и само `return` — перенос строки сразу после `return` завершает оператор без значения;
- при переносе аргументов вызова/массива на несколько строк последний элемент перед закрывающей `)`/`]` на отдельной строке должен заканчиваться запятой (trailing comma) — иначе ASI вставит `;` прямо перед скобкой и разорвёт выражение.

Явный `;` по-прежнему допустим — нужен для нескольких операторов на одной строке. `}`, будучи ASI-триггером, может породить `;` там, где грамматика формально его не требует (например, после блока `if`) — раздел 6 объясняет, как это учтено.

---

## 2. Программа

```
Program              ::= { TopLevelDeclaration | ";" }

TopLevelDeclaration  ::= MoldDeclaration
                        | FacetDeclaration
                        | FunctionDeclaration
```

Ровно одна функция верхнего уровня должна называться `main`, не принимать параметров и не иметь `-> Type` — точка входа (проверка семантическая, не грамматическая).

Отдельная `";"` допущена и здесь, и в `Statement` (раздел 6) по одной и той же причине: `}` — ASI-триггер, поэтому лексер вставляет `;` после закрывающей скобки *любого* блока или декларации, а не только там, где точка с запятой грамматически обязательна (например, после `if (...) { }` или после `mold X { }`). Простее дать парсеру молча поглощать одиночную `;` как «ничего не делать», чем заставлять лексер понимать грамматику.

---

## 3. Molds и facets

`mold` — то, что в C#/Java называется `class`: определяет «форму», по которой создаются объекты. `facet` — то, что называется `interface`: контракт, «грань», которую mold обязуется предъявить.

```
MoldDeclaration        ::= "mold" identifier [ GenericParams ]
                            [ ":" TypeName { "," TypeName } ]
                            "{" { MoldMember } "}"

FacetDeclaration       ::= "facet" identifier [ GenericParams ]
                            [ ":" TypeName { "," TypeName } ]
                            "{" { FacetMethodSig } "}"

GenericParams           ::= "<" identifier { "," identifier } ">"

MoldMember              ::= FieldDeclaration
                           | MethodDeclaration
                           | ConstructorDeclaration

FieldDeclaration        ::= [ AccessModifier ] [ "static" ] "let" identifier ":" Type [ "=" Expression ] ";"

MethodDeclaration       ::= [ AccessModifier ] [ "static" | "virtual" | "override" ]
                             "fn" identifier [ GenericParams ]
                             "(" [ ParamList ] ")" [ "->" Type ] Block

ConstructorDeclaration  ::= [ AccessModifier ] identifier "(" [ ParamList ] ")" Block

FacetMethodSig          ::= "fn" identifier "(" [ ParamList ] ")" [ "->" Type ] ";"

AccessModifier           ::= "public" | "private" | "protected"
```

**Семантика поверх грамматики:**
- Mold без явного `:` неявно наследует встроенный `Glass` — корневой тип. В языке буквально всё «стекло»: каждый объект по умолчанию прозрачен для интроспекции (раздел 8).
- После `:` список типов может содержать не более одного mold'а (базовый), остальные обязаны быть facet'ами. Что есть что — решает семантический анализ по таблице символов, не грамматика.
- **Виртуальный метод** — тот, что попадает в vtable и вызывается по *реальному* классу объекта: метод с `virtual` или `override`; метод, реализующий контракт facet'а (автоматически виртуален); `toString()` без параметров (см. ниже). Обычный метод связывается по *статическому* типу переменной.
- `override` допустим только над виртуальным методом базового mold'а с той же сигнатурой. Над обычным методом — ошибка: иначе вызов через базовый тип молча не дошёл бы до наследника.
- Метод наследника **без** `override`, совпадающий по сигнатуре с виртуальным методом базы, — ошибка «добавьте override»: иначе он остался бы мёртвым кодом вне vtable. Сокрытие обычного метода обычным допустимо (вызов идёт по статическому типу).
- **`toString`.** Метод `fn toString() -> string` без параметров неявно виртуален. Его вызывают `print`, `+` со строкой, `+=` к строке, `str(x)` и `(string) x` для значений ссылочного типа — по реальному классу объекта. Без `toString` объект печатается как `Класс@0x…` (hex-адрес, тот же, что возвращает `~address`), `null` — как `null`. `toString` с другой сигнатурой — ошибка. Элементы массива печатаются без вызова `toString`.
- **Приведение ссылочных типов.** `(Dog) animal` проверяется во время выполнения по реальному классу объекта: проходит, если объект — экземпляр `Dog`, его потомка или реализует facet `Dog`; `null` проходит всегда; иначе — ошибка выполнения «нельзя привести объект класса 'Cat' к 'Dog'». На этапе типов отсекается заведомо невозможное: число/строка/массив → mold, а также два mold'а, не связанных наследованием.
- `static`-метод не попадает в vtable.
- Вызов конструктора базового mold'а — `super(args)` первой строкой тела конструктора (раздел 7), не отдельная синтаксическая конструкция.

---

## 4. Функции

```
FunctionDeclaration ::= "fn" identifier [ GenericParams ]
                         "(" [ ParamList ] ")" [ "->" Type ] Block

ParamList             ::= Param { "," Param }
Param                  ::= identifier ":" Type
```

Отсутствие `-> Type` означает, что функция ничего не возвращает — отдельного ключевого слова `void` в языке нет.

---

## 5. Типы

```
Type            ::= PrimitiveType
                   | TypeName [ GenericArgs ]
                   | "[" Type "]"

PrimitiveType   ::= "int" | "double" | "bool" | "string"

TypeName        ::= identifier

GenericArgs     ::= "<" Type { "," Type } ">"
```

`int`, `double`, `bool` — типы значений на стеке VM. Всё остальное, включая `string` и массивы `[Type]`, — ссылочные типы в куче.

---

## 6. Операторы (statements)

```
Block                ::= "{" { Statement } "}"

Statement            ::= LetStatement
                        | IfStatement
                        | WhileStatement
                        | ForStatement
                        | ReturnStatement
                        | "break" ";"
                        | "continue" ";"
                        | TryStatement
                        | "throw" Expression ";"
                        | ExpressionStatement
                        | Block
                        | ";"

LetStatement         ::= "let" identifier ":" Type [ "=" Expression ] ";"
                        | "let" identifier "=" Expression ";"

IfStatement          ::= "if" Expression Block [ "else" Statement ]

WhileStatement       ::= "while" Expression Block

ForStatement         ::= "for" identifier "in" ForRange Block
ForRange              ::= Expression [ ".." Expression ]

ReturnStatement      ::= "return" [ Expression ] ";"

TryStatement         ::= "try" Block { "catch" identifier ":" Type Block } [ "finally" Block ]

ExpressionStatement  ::= Expression ";"
```

Условия `if`/`while` и итерируемое выражение `for` не требуют скобок вокруг себя. `else` привязывается к ближайшему незакрытому `if`. `..` внутри `for` — диапазон (`0..10` — от 0 включительно до 10 исключительно); как самостоятельное выражение вне `for` диапазон недоступен — сознательное ограничение ради простоты.

---

## 7. Выражения

**Приоритет операций** (от низшего к высшему):
1. `=` `+=` `-=` `*=` `/=` — присваивание (справа налево)
2. `||`
3. `&&`
4. `==` `!=`
5. `<` `>` `<=` `>=`
6. `+` `-` бинарные
7. `*` `/` `%`
8. `!` `-` унарные, `(Type)` приведение (справа налево)
9. `.` `()` `[]` `~` — доступ, вызов, индексация, интроспекция

```
Expression        ::= Assignment

Assignment        ::= LogicalOr [ ( "=" | "+=" | "-=" | "*=" | "/=" ) Assignment ]

LogicalOr         ::= LogicalAnd { "||" LogicalAnd }
LogicalAnd        ::= Equality { "&&" Equality }
Equality          ::= Relational { ( "==" | "!=" ) Relational }
Relational        ::= Additive { ( "<" | ">" | "<=" | ">=" ) Additive }
Additive          ::= Multiplicative { ( "+" | "-" ) Multiplicative }
Multiplicative    ::= Unary { ( "*" | "/" | "%" ) Unary }

Unary             ::= ( "!" | "-" ) Unary
                     | "(" Type ")" Unary
                     | Postfix

Postfix           ::= Primary { PostfixOp }
PostfixOp         ::= "." identifier [ "(" [ ArgList ] ")" ]
                     | "[" Expression "]"
                     | "(" [ ArgList ] ")"
                     | "~" IntrospectField

IntrospectField   ::= "layout" | "vtable" | "address"

Primary           ::= int_literal | double_literal | bool_literal | string_literal | null_literal
                     | "this"
                     | "super" [ "(" [ ArgList ] ")" | "." identifier "(" [ ArgList ] ")" ]
                     | identifier [ GenericArgs ]
                     | "[" [ ArgList ] "]"
                     | "~" GlobalIntrospect
                     | "(" Expression ")"

GlobalIntrospect  ::= "heap" | "gc"

ArgList           ::= Expression { "," Expression }
```

Конструктор вызывается без `new`: `Circle(3.0)` синтаксически — обычный вызов (`identifier` + `(...)`), различие «это создание объекта или вызов функции» разрешается семантически по таблице символов, не грамматикой. `[a, b, c]` — литерал массива, `[]` — пустой. Битовых операторов (`&`, `|`, `^`, `<<`, `>>`) в языке нет — символ `~` целиком закреплён за интроспекцией.

---

## 8. Интроспекция и графика

```
d~layout      // раскладка полей объекта d: имя, смещение, размер
d~vtable      // таблица виртуальных методов d
d~address     // адрес d в куче
~heap         // снапшот кучи целиком
~gc           // запустить сборку мусора вручную
```

Графика и интерактивность — не отдельная грамматическая конструкция, а обычные molds стандартной библиотеки:

```
mold Canvas : Glass {
    fn clear()
    fn fillCircle(x: double, y: double, r: double, c: Color)
    fn fillRect(x: double, y: double, w: double, h: double, c: Color)
    fn drawLine(x1: double, y1: double, x2: double, y2: double, c: Color)
    fn drawText(x: double, y: double, text: string, c: Color)
}

mold Color : Glass {
    Color(r: int, g: int, b: int)
}

mold Scene : Glass {
    virtual fn onFrame(canvas: Canvas) { }
    virtual fn onMouseMove(x: double, y: double) { }
    virtual fn onMouseDown(x: double, y: double, button: string) { }
    virtual fn onKeyPress(key: string) { }
}
```

Студент наследует `Scene` и переопределяет нужные хуки (пример — раздел 11). Встроенная функция `run(scene: Scene)` передаёт управление раннтайму: он вызывает `onFrame` каждый кадр и `onMouseMove`/`onMouseDown`/`onKeyPress` по событиям — это обычные виртуальные вызовы через vtable, а не особый механизм, поэтому графика служит демонстрацией диспетчеризации, а не только визуального результата. VM ничего не рендерит сама: команды рисования идут через те же хуки, что уже ведут в WPF для визуализации памяти, — рисует WPF-Canvas на стороне IDE.

---

## 9. Известные неоднозначности

**Угловые скобки дженериков.** `a < b > c` — сравнение или начало `Foo<Bar>`? Разбирается жадно как `GenericArgs`, когда слева — идентификатор в позиции типа/вызова; при неудаче парсер откатывается и читает как цепочку сравнений.

**Dangling else.** `else` всегда относится к ближайшему `if` без пары.

**ASI и многострочные выражения.** Перенос строки не завершает выражение, если оно явно не закончено (последний токен — оператор, открывающая скобка, запятая) — см. правило в разделе 1. Исключение, о котором нужно помнить: `return` без значения на той же строке сразу завершается переносом строки.

---

## 10. Сознательно не включено в v1

- **Множественное наследование molds** — только facet'ы.
- **Функции как значения** — без замыканий, лямбд, делегатов.
- **Nullable-аннотации, паттерн-матчинг, пользовательская перегрузка операторов.**
- **Битовые операторы** (`&`, `|`, `^`, `<<`, `>>`) — `~` полностью закреплён за интроспекцией.
- **Ограничения дженериков (generic constraints)** — только стирание типов + boxing примитивов.
- **Диапазоны как самостоятельный тип значения** — `a..b` работает только внутри `for`.
- **Пространства имён / модули.**
- **Обобщённый протокол итераторов** — `for..in` работает по массивам, `List<T>` и диапазонам, не по произвольным пользовательским итераторам.
- **Файловый I/O в стандартной библиотеке v1** — только консоль, коллекции и графика (раздел 8).
- **Сеть, потоки/конкурентность.**
- **Generational GC** — только mark-and-sweep, без исключений на будущее в рамках диплома.

---

## 11. Пример программы

```
facet Shape {
    fn area() -> double
}

mold Circle : Shape {
    let radius: double

    Circle(r: double) {
        radius = r
    }

    fn area() -> double {
        return 3.14159 * radius * radius
    }
}

mold Square : Shape {
    let side: double

    Square(s: double) {
        side = s
    }

    fn area() -> double {
        return side * side
    }
}

fn totalArea(shapes: [Shape]) -> double {
    let total: double = 0
    for s in shapes {
        total += s.area()
    }
    return total
}

fn main() {
    let shapes: [Shape] = [Circle(3.0), Square(4.0)]

    try {
        print(totalArea(shapes))
        print(shapes[0]~vtable)
        print(shapes[0]~address)
    } catch e: Exception {
        print("Ошибка: " + e.message)
    }
}
```

Интерактивный пример — Scene с обработкой мыши:

```
mold Game : Scene {
    let x: double
    let y: double

    override fn onFrame(canvas: Canvas) {
        canvas.clear()
        canvas.fillCircle(x, y, 20.0, Color(0, 120, 255))
    }

    override fn onMouseMove(x: double, y: double) {
        this.x = x
        this.y = y
    }
}

fn main() {
    run(Game())
}
```

`print`, `List<T>`, `Exception` — часть стандартной библиотеки, здесь используются для полноты примеров.
