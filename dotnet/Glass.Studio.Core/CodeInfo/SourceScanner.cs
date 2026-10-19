// Лёгкий сканер исходника Glass для подсказок в редакторе: делит текст на лексемы с позициями.
// Это НЕ полноценный лексер компилятора (тот — на C++ внутри нативной библиотеки): подсказкам нужны только
// границы слов, их вид и строки/столбцы, и нужно, чтобы сканер работал на недописанном, «сломанном» коде.

using System;
using System.Collections.Generic;

namespace Glass.Studio.CodeInfo
{
    public enum TokenKind { Identifier, Keyword, Number, String, Comment, Operator, Introspection }

    public sealed record SourceToken(TokenKind Kind, int Start, int Length, int Line, int Column)
    {
        public int End => Start + Length;
        public string Text(string source) => source.Substring(Start, Length);
    }

    public static class SourceScanner
    {
        /// <summary>Ключевые слова — ровно таблица лексера (проверяется тестом против Lexer.cpp), кроме слов интроспекции.</summary>
        public static readonly HashSet<string> Keywords = new HashSet<string>
        {
            "mold", "facet", "fn", "let", "super", "this", "static", "virtual", "override", "public", "private", "protected",
            "if", "else", "while", "for", "in", "return", "break", "continue", "try", "catch", "finally", "throw",
            "true", "false", "null", "int", "double", "bool", "string",
        };

        public static readonly HashSet<string> IntrospectionWords = new HashSet<string> { "layout", "vtable", "address", "heap", "gc" };

        private static readonly string[] TwoCharOperators = { "..", "->", "==", "!=", "<=", ">=", "&&", "||", "+=", "-=", "*=", "/=" };

        public static IReadOnlyList<SourceToken> Scan(string source)
        {
            var tokens = new List<SourceToken>();
            int n = source.Length, i = 0, line = 1, lineStart = 0;

            while (i < n)
            {
                char c = source[i];
                if (c == '\n') { line++; i++; lineStart = i; continue; }
                if (char.IsWhiteSpace(c)) { i++; continue; }

                int start = i;
                int column = start - lineStart + 1;

                if (c == '/' && i + 1 < n && source[i + 1] == '/')
                {
                    while (i < n && source[i] != '\n') i++;
                    tokens.Add(new SourceToken(TokenKind.Comment, start, i - start, line, column));
                }
                else if (c == '"')
                {
                    i++;
                    while (i < n && source[i] != '"' && source[i] != '\n')
                    {
                        if (source[i] == '\\' && i + 1 < n && source[i + 1] != '\n') i++;
                        i++;
                    }
                    if (i < n && source[i] == '"') i++;   // незакрытая строка кончается на конце строки текста
                    tokens.Add(new SourceToken(TokenKind.String, start, i - start, line, column));
                }
                else if (char.IsDigit(c))
                {
                    while (i < n && char.IsDigit(source[i])) i++;
                    if (i + 1 < n && source[i] == '.' && char.IsDigit(source[i + 1]))   // "0..5" — это диапазон, а не дробь
                    {
                        i++;
                        while (i < n && char.IsDigit(source[i])) i++;
                    }
                    if (i < n && (source[i] == 'e' || source[i] == 'E'))
                    {
                        int j = i + 1;
                        if (j < n && (source[j] == '+' || source[j] == '-')) j++;
                        if (j < n && char.IsDigit(source[j]))
                        {
                            i = j;
                            while (i < n && char.IsDigit(source[i])) i++;
                        }
                    }
                    tokens.Add(new SourceToken(TokenKind.Number, start, i - start, line, column));
                }
                else if (char.IsLetter(c) || c == '_')
                {
                    while (i < n && (char.IsLetterOrDigit(source[i]) || source[i] == '_')) i++;
                    string word = source.Substring(start, i - start);
                    tokens.Add(new SourceToken(Keywords.Contains(word) ? TokenKind.Keyword : TokenKind.Identifier, start, i - start, line, column));
                }
                else if (c == '~')
                {
                    int j = i + 1;
                    while (j < n && char.IsLetter(source[j])) j++;
                    if (j > i + 1 && IntrospectionWords.Contains(source.Substring(i + 1, j - i - 1)))
                    {
                        i = j;
                        tokens.Add(new SourceToken(TokenKind.Introspection, start, i - start, line, column));
                    }
                    else
                    {
                        i++;
                        tokens.Add(new SourceToken(TokenKind.Operator, start, 1, line, column));
                    }
                }
                else
                {
                    int length = 1;
                    if (i + 1 < n)
                    {
                        string two = source.Substring(i, 2);
                        if (Array.IndexOf(TwoCharOperators, two) >= 0) length = 2;
                    }
                    i += length;
                    tokens.Add(new SourceToken(TokenKind.Operator, start, length, line, column));
                }
            }
            return tokens;
        }
    }
}
