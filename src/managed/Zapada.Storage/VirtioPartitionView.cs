namespace Zapada.Storage
{
    public sealed class VirtioPartitionView : PartitionView
    {
        private long _startLba;
        private long _sectorCount;
        private int _schemeKind;

        public void Initialize(long startLba, long sectorCount, int schemeKind)
        {
            _startLba = startLba;
            _sectorCount = sectorCount;
            _schemeKind = schemeKind;
        }

        public override long GetStartLba() { return _startLba; }
        public override long GetSectorCount() { return _sectorCount; }
        public override int GetSchemeKind() { return _schemeKind; }

        public override int ReadSectors(long lba, int sectorCount, byte[] buffer, int bufferOffset)
        {
            if (buffer == null || sectorCount <= 0 || bufferOffset < 0)
                return StorageStatus.InvalidArgument;

            if (lba < 0 || lba + sectorCount > _sectorCount)
                return StorageStatus.InvalidArgument;

            int byteCount = sectorCount * 512;
            if (bufferOffset + byteCount > buffer.Length)
                return StorageStatus.InvalidArgument;

            int[] intBuf = new int[sectorCount * 128];
            long absoluteLba = _startLba + lba;
            int rc = Zapada.BlockDev.ReadSector(absoluteLba, sectorCount, intBuf);
            if (rc != 0)
                return StorageStatus.IoError;

            for (int i = 0; i < byteCount; i++)
                buffer[bufferOffset + i] = (byte)((intBuf[i / 4] >> ((i % 4) * 8)) & 0xFF);

            return sectorCount;
        }
    }
}
