package utils;

/** In-memory counterpart of DeXOR's official StreamReader. */
public class StreamReader {
    private byte[] buffer = new byte[0];
    private int bitOffset = 0;

    public StreamReader(String ignoredInputPath) {
    }

    public void setBuffer(byte[] data) {
        buffer = data;
        bitOffset = 0;
    }

    public long readLong(int size) {
        if (size < 0 || size > 64 || bitOffset + size > buffer.length * 8) {
            throw new IllegalStateException("Invalid DeXOR bitstream read");
        }
        long result = 0;
        int remaining = size;
        while (remaining > 0) {
            int byteIndex = bitOffset >>> 3;
            int offsetInByte = bitOffset & 7;
            int available = 8 - offsetInByte;
            int take = Math.min(remaining, available);
            int shift = available - take;
            int mask = (1 << take) - 1;
            int chunk = ((buffer[byteIndex] & 0xff) >>> shift) & mask;
            result = (result << take) | chunk;
            bitOffset += take;
            remaining -= take;
        }
        return result;
    }

    public int readInt(int size) {
        return (int) readLong(size);
    }

    public float readFloat(int size) {
        return Float.intBitsToFloat((int) readLong(size));
    }

    public double readDouble(int size) {
        return Double.longBitsToDouble(readLong(size));
    }

    public boolean readBoolean() {
        return readLong(1) > 0;
    }
}
