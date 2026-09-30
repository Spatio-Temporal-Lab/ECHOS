package utils;

import java.io.ByteArrayOutputStream;
import java.util.Arrays;

/**
 * In-memory counterpart of DeXOR's official StreamWriter.
 *
 * The public write and bit-accounting semantics match the upstream class. A
 * call to clear() closes the current segment and starts a byte-aligned segment
 * without resetting the DeXOR algorithm state.
 */
public class StreamWriter {
    private final ByteArrayOutputStream buffer = new ByteArrayOutputStream(512);
    private int currentByte = 0;
    private int leftBits = 8;
    private int deltaBits = 0;
    private byte[] completedBlock = new byte[0];

    public StreamWriter(String ignoredOutputPath) {
    }

    private void saveByte() {
        if (leftBits == 8) {
            return;
        }
        currentByte <<= leftBits;
        buffer.write((byte) currentByte);
        leftBits = 8;
        currentByte = 0;
    }

    public int track_bits() {
        int bits = deltaBits;
        deltaBits = 0;
        return bits;
    }

    public void clear() {
        saveByte();
        completedBlock = buffer.toByteArray();
        buffer.reset();
        leftBits = 8;
        currentByte = 0;
    }

    public byte[] getCompletedBlock() {
        return Arrays.copyOf(completedBlock, completedBlock.length);
    }

    public void write(boolean value) {
        deltaBits += 1;
        leftBits--;
        currentByte = (currentByte << 1) | (value ? 1 : 0);
        if (leftBits == 0) {
            saveByte();
        }
    }

    public void write(long value, int size) {
        deltaBits += Math.max(0, size);
        while (size > 0) {
            int len = Math.min(leftBits, size);
            int mask = (1 << len) - 1;
            currentByte <<= len;
            currentByte |= (byte) ((value >> (size - len)) & mask);
            leftBits -= len;
            if (leftBits == 0) {
                saveByte();
            }
            size -= len;
        }
    }

    public void write(int value, int size) {
        write((long) value, size);
    }

    public void write(float value, int size) {
        write((long) Float.floatToRawIntBits(value), size);
    }

    public void write(double value, int size) {
        write(Double.doubleToRawLongBits(value), size);
    }
}
