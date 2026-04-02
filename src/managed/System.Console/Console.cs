// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Text;
using System.Threading;
using Microsoft.Win32.SafeHandles;

namespace System
{
    public static class Console
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine();

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine(bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine(char value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine(char[]? buffer);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine(char[] buffer, int index, int count);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine(decimal value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine(double value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine(float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine(int value);

        [CLSCompliant(false)]
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine(uint value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine(long value);

        [CLSCompliant(false)]
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine(ulong value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine(object? value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine(string? value);

        /// <summary>
        /// Writes the specified read-only span of characters, followed by the current line terminator, to the standard output stream.
        /// </summary>
        /// <param name="value">The span of characters to write.</param>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine(ReadOnlySpan<char> value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine([StringSyntax(StringSyntaxAttribute.CompositeFormat)] string format, object? arg0);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine([StringSyntax(StringSyntaxAttribute.CompositeFormat)] string format, object? arg0, object? arg1);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine([StringSyntax(StringSyntaxAttribute.CompositeFormat)] string format, object? arg0, object? arg1, object? arg2);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine([StringSyntax(StringSyntaxAttribute.CompositeFormat)] string format, params object?[]? arg);

        /// <summary>
        /// Writes the text representation of the specified span of objects, followed by the current line terminator, to the standard output stream using the specified format information.
        /// </summary>
        /// <param name="format">A composite format string.</param>
        /// <param name="arg">A span of objects to write using format.</param>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void WriteLine([StringSyntax(StringSyntaxAttribute.CompositeFormat)] string format, params ReadOnlySpan<object?> arg);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Write([StringSyntax(StringSyntaxAttribute.CompositeFormat)] string format, object? arg0);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Write([StringSyntax(StringSyntaxAttribute.CompositeFormat)] string format, object? arg0, object? arg1);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Write([StringSyntax(StringSyntaxAttribute.CompositeFormat)] string format, object? arg0, object? arg1, object? arg2);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Write([StringSyntax(StringSyntaxAttribute.CompositeFormat)] string format, params object?[]? arg);

        /// <summary>
        /// Writes the text representation of the specified span of objects to the standard output stream using the specified format information.
        /// </summary>
        /// <param name="format">A composite format string.</param>
        /// <param name="arg">A span of objects to write using format.</param>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Write([StringSyntax(StringSyntaxAttribute.CompositeFormat)] string format, params ReadOnlySpan<object?> arg);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Write(bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Write(char value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Write(char[]? buffer);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Write(char[] buffer, int index, int count);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Write(double value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Write(decimal value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Write(float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Write(int value);

        [CLSCompliant(false)]
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Write(uint value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Write(long value);

        [CLSCompliant(false)]
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Write(ulong value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Write(object? value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Write(string? value);

        /// <summary>
        /// Writes the specified read-only span of characters to the standard output stream.
        /// </summary>
        /// <param name="value">The span of characters to write.</param>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Write(ReadOnlySpan<char> value);
    }
}
