namespace Zapada.Fs.Ext;

public static class ExtText
{
    public static string ReadFixedAscii(byte[] buffer, int offset, int length)
    {
        string text = "";
        for (int i = 0; i < length; i++)
        {
            int value = ExtLittleEndian.ReadByte(buffer, offset + i);
            if (value == 0)
                break;

            text = string.Concat(text, ByteToString(value));
        }

        return text;
    }

    public static string ByteToString(int value)
    {
        if (value == 0x20) return " ";
        if (value == 0x21) return "!";
        if (value == 0x23) return "#";
        if (value == 0x24) return "$";
        if (value == 0x25) return "%";
        if (value == 0x26) return "&";
        if (value == 0x27) return "'";
        if (value == 0x28) return "(";
        if (value == 0x29) return ")";
        if (value == 0x2D) return "-";
        if (value == 0x2E) return ".";
        if (value == 0x2F) return "/";
        if (value == 0x30) return "0";
        if (value == 0x31) return "1";
        if (value == 0x32) return "2";
        if (value == 0x33) return "3";
        if (value == 0x34) return "4";
        if (value == 0x35) return "5";
        if (value == 0x36) return "6";
        if (value == 0x37) return "7";
        if (value == 0x38) return "8";
        if (value == 0x39) return "9";
        if (value == 0x40) return "@";
        if (value == 0x41) return "A";
        if (value == 0x42) return "B";
        if (value == 0x43) return "C";
        if (value == 0x44) return "D";
        if (value == 0x45) return "E";
        if (value == 0x46) return "F";
        if (value == 0x47) return "G";
        if (value == 0x48) return "H";
        if (value == 0x49) return "I";
        if (value == 0x4A) return "J";
        if (value == 0x4B) return "K";
        if (value == 0x4C) return "L";
        if (value == 0x4D) return "M";
        if (value == 0x4E) return "N";
        if (value == 0x4F) return "O";
        if (value == 0x50) return "P";
        if (value == 0x51) return "Q";
        if (value == 0x52) return "R";
        if (value == 0x53) return "S";
        if (value == 0x54) return "T";
        if (value == 0x55) return "U";
        if (value == 0x56) return "V";
        if (value == 0x57) return "W";
        if (value == 0x58) return "X";
        if (value == 0x59) return "Y";
        if (value == 0x5A) return "Z";
        if (value == 0x5F) return "_";
        if (value == 0x61) return "a";
        if (value == 0x62) return "b";
        if (value == 0x63) return "c";
        if (value == 0x64) return "d";
        if (value == 0x65) return "e";
        if (value == 0x66) return "f";
        if (value == 0x67) return "g";
        if (value == 0x68) return "h";
        if (value == 0x69) return "i";
        if (value == 0x6A) return "j";
        if (value == 0x6B) return "k";
        if (value == 0x6C) return "l";
        if (value == 0x6D) return "m";
        if (value == 0x6E) return "n";
        if (value == 0x6F) return "o";
        if (value == 0x70) return "p";
        if (value == 0x71) return "q";
        if (value == 0x72) return "r";
        if (value == 0x73) return "s";
        if (value == 0x74) return "t";
        if (value == 0x75) return "u";
        if (value == 0x76) return "v";
        if (value == 0x77) return "w";
        if (value == 0x78) return "x";
        if (value == 0x79) return "y";
        if (value == 0x7A) return "z";

        return "?";
    }
}
