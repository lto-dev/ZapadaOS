using System.Runtime.CompilerServices;

namespace System
{
    //public static class Console
    //{
    //    [MethodImpl(MethodImplOptions.InternalCall)]
    //    public static extern void Write(string s);

    //    [MethodImpl(MethodImplOptions.InternalCall)]
    //    public static extern void Write(int value);

    //    [MethodImpl(MethodImplOptions.InternalCall)]
    //    public static extern void WriteLine(string s);
    //}

    public class Object
    {
        public Object() { }
        public virtual string ToString() => string.Empty;
        public virtual bool Equals(object obj) => this == obj;
        public virtual int GetHashCode() => 0;
    }

    public struct Void { }

    public abstract class ValueType : object { }

    public abstract class Enum : ValueType { }

    public abstract class Array : object
    {
        public int Length { [Runtime.CompilerServices.MethodImpl(Runtime.CompilerServices.MethodImplOptions.InternalCall)] get; }

        private static class EmptyArray<T>
        {
            internal static readonly T[] Value = new T[0];
        }

        public static T[] Empty<T>()
        {
            return EmptyArray<T>.Value;
        }
    }

    public abstract class Delegate : object { }

    public abstract class MulticastDelegate : Delegate { }

    public struct Boolean { private readonly bool _value; }
    public struct Char { private readonly char _value; }
    public struct Int16 { private readonly short _value; public const short MaxValue = 32767; public const short MinValue = unchecked((short)0x8000); }
    public struct Int32 { private readonly int _value; public const int MaxValue = 2147483647; public const int MinValue = unchecked((int)0x80000000); }
    public struct UInt32 { private readonly uint _value; public const uint MaxValue = 0xFFFFFFFFu; public const uint MinValue = 0u; }
    public struct Int64 { private readonly long _value; public const long MaxValue = 9223372036854775807L; public const long MinValue = unchecked((long)0x8000000000000000UL); }
    public struct UInt64 { private readonly ulong _value; public const ulong MaxValue = 0xFFFFFFFFFFFFFFFFUL; public const ulong MinValue = 0UL; }
    public struct UIntPtr { private readonly unsafe void* _value; }
    public struct IntPtr { private readonly unsafe void* _value; }
    public struct RuntimeFieldHandle { }
    public struct RuntimeMethodHandle { }
    public struct RuntimeTypeHandle { }
    public struct RuntimeArgumentHandle { }
    public struct TypedReference { }

    public class Attribute : object { }

    [AttributeUsage(AttributeTargets.All, Inherited = false)]
    public sealed class CLSCompliantAttribute : Attribute
    {
        public CLSCompliantAttribute(bool isCompliant) { }
    }

    [AttributeUsage(AttributeTargets.All, Inherited = false)]
    public sealed class ParamArrayAttribute : Attribute
    {
        public ParamArrayAttribute() { }
    }

    [AttributeUsage(AttributeTargets.All, Inherited = false)]
    public sealed class SerializableAttribute : Attribute
    {
        public SerializableAttribute() { }
    }

    [AttributeUsage(AttributeTargets.All, Inherited = false)]
    public sealed class FlagsAttribute : Attribute
    {
        public FlagsAttribute() { }
    }

    [AttributeUsage(AttributeTargets.All, Inherited = false)]
    public sealed class ObsoleteAttribute : Attribute
    {
        public ObsoleteAttribute() { }
        public ObsoleteAttribute(string message) { }
        public ObsoleteAttribute(string message, bool error) { }
        public string DiagnosticId { get; set; }
        public string UrlFormat { get; set; }
    }

    [Flags]
    public enum AttributeTargets
    {
        Assembly = 1,
        Module = 2,
        Class = 4,
        Struct = 8,
        Enum = 16,
        Constructor = 32,
        Method = 64,
        Property = 128,
        Field = 256,
        Event = 512,
        Interface = 1024,
        Parameter = 2048,
        Delegate = 4096,
        ReturnValue = 8192,
        GenericParameter = 16384,
        All = 32767
    }

    public sealed class AttributeUsageAttribute : Attribute
    {
        public AttributeUsageAttribute(AttributeTargets validOn)
        {
            ValidOn = validOn;
        }

        public AttributeTargets ValidOn { get; }
        public bool AllowMultiple { get; set; }
        public bool Inherited { get; set; }
    }

    public class Exception : object
    {
        public Exception() { }
        public Exception(string message) { Message = message; }
        public Exception(string message, Exception innerException) { Message = message; InnerException = innerException; }
        public virtual string Message { get; }
        public Exception InnerException { get; }
    }

    public class SystemException : Exception
    {
        public SystemException() { }
        public SystemException(string message) : base(message) { }
        public SystemException(string message, Exception innerException) : base(message, innerException) { }
    }

    public class ArithmeticException : SystemException
    {
        public ArithmeticException() { }
        public ArithmeticException(string message) : base(message) { }
    }

    public class DivideByZeroException : ArithmeticException
    {
        public DivideByZeroException() { }
        public DivideByZeroException(string message) : base(message) { }
    }

    public class OverflowException : ArithmeticException
    {
        public OverflowException() { }
        public OverflowException(string message) : base(message) { }
    }

    public static class ThrowHelper
    {
        [Diagnostics.StackTraceHidden]
        public static void ThrowDivideByZeroException() => throw new DivideByZeroException();

        [Diagnostics.StackTraceHidden]
        public static void ThrowOverflowException() => throw new OverflowException();
    }

    public sealed class String : object
    {
        public static readonly string Empty = "";

        [Runtime.CompilerServices.MethodImpl(Runtime.CompilerServices.MethodImplOptions.InternalCall)]
        public extern String(char[] value);

        [Runtime.CompilerServices.MethodImpl(Runtime.CompilerServices.MethodImplOptions.InternalCall)]
        public extern String(char[] value, int startIndex, int length);

        [Runtime.CompilerServices.MethodImpl(Runtime.CompilerServices.MethodImplOptions.InternalCall)]
        public extern String(char c, int count);

        public extern int Length
        {
            [Runtime.CompilerServices.MethodImpl(Runtime.CompilerServices.MethodImplOptions.InternalCall)]
            get;
        }

        [System.Runtime.CompilerServices.IndexerName("Chars")]
        public extern char this[int index]
        {
            [Runtime.CompilerServices.MethodImpl(Runtime.CompilerServices.MethodImplOptions.InternalCall)]
            get;
        }

        [Runtime.CompilerServices.MethodImpl(Runtime.CompilerServices.MethodImplOptions.InternalCall)]
        public extern string Substring(int startIndex);

        [Runtime.CompilerServices.MethodImpl(Runtime.CompilerServices.MethodImplOptions.InternalCall)]
        public extern string Substring(int startIndex, int length);
    }

    public static partial class Math
    {
    }
}

namespace System.Diagnostics.CodeAnalysis
{
    public sealed class RequiresUnsafeAttribute : global::System.Attribute { }
}

namespace System.Runtime.InteropServices
{
    public enum LayoutKind
    {
        Sequential = 0,
        Explicit = 2,
        Auto = 3
    }

    public sealed class StructLayoutAttribute : global::System.Attribute
    {
        public StructLayoutAttribute(LayoutKind layoutKind)
        {
            Value = layoutKind;
        }

        public LayoutKind Value { get; }
    }
}

namespace System.Runtime.CompilerServices
{
    [global::System.Flags]
    public enum MethodImplOptions
    {
        Unmanaged = 4,
        NoInlining = 8,
        ForwardRef = 16,
        Synchronized = 32,
        NoOptimization = 64,
        PreserveSig = 128,
        AggressiveInlining = 256,
        AggressiveOptimization = 512,
        InternalCall = 4096
    }

    public enum MethodCodeType
    {
        IL = 0,
        Native = 1,
        OPTIL = 2,
        Runtime = 3
    }

    [global::System.AttributeUsage(global::System.AttributeTargets.Method | global::System.AttributeTargets.Constructor, Inherited = false)]
    public sealed class MethodImplAttribute : global::System.Attribute
    {
        public MethodImplAttribute() { }
        public MethodImplAttribute(MethodImplOptions methodImplOptions) { Value = methodImplOptions; }
        public MethodImplAttribute(short value) { Value = (MethodImplOptions)value; }
        public MethodCodeType MethodCodeType;
        public MethodImplOptions Value { get; }
    }
}

namespace System.Threading
{
    public sealed class Thread : global::System.Object
    {
        [global::System.Runtime.CompilerServices.MethodImpl(global::System.Runtime.CompilerServices.MethodImplOptions.InternalCall)]
        internal static extern bool CurrentThreadIsFinalizerThread();
    }
}

namespace System.Reflection
{
    public sealed class DefaultMemberAttribute : global::System.Attribute
    {
        public DefaultMemberAttribute(string memberName) { }
    }
}

namespace System.Numerics
{
}
