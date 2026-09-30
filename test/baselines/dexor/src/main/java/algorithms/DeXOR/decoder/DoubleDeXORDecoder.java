package algorithms.DeXOR.decoder;

import algorithms.Decoder;
import algorithms.DeXOR.DeXORTools;
import enums.DataTypeEnums;

/** Official DeXOR double decoder at upstream commit 06128b3. */
public class DoubleDeXORDecoder extends Decoder {
    protected int size = DataTypeEnums.DOUBLE.getSize();
    protected double previous_value = 0;
    protected int previous_q = 0;
    protected int previous_delta = 0;
    protected long previous_exp = 1023;
    protected int EL = 1;
    protected int contract_step = 0;
    protected double previous_alpha = 0;
    protected Method method = new Native();
    protected boolean skip = false;
    protected int buffer_bits = 0;
    protected double[] buffer = new double[0];
    protected int rho = 8;
    protected int skip_available = -1;

    public DoubleDeXORDecoder(String inputPath) {
        super(inputPath);
    }

    public DoubleDeXORDecoder(String inputPath, String config) {
        super(inputPath, config);
        String bufferBitsConfig = this.config.get("buffer_bits");
        String rhoConfig = this.config.get("rho");
        String skipConfig = this.config.get("skip_available");
        if (bufferBitsConfig != null) buffer_bits = Integer.parseInt(bufferBitsConfig);
        if (rhoConfig != null) rho = Integer.parseInt(rhoConfig);
        if (skipConfig != null) skip_available = Integer.parseInt(skipConfig);
        if (buffer_bits > 0) {
            buffer = new double[1 << buffer_bits];
            method = new Buffered();
        } else if (skip_available >= 0) {
            method = new Skippable();
        }
    }

    protected double ExceptionDecode() {
        int bias = DeXORTools.getP2(EL - 1) - 1;
        long delta = in.readInt(EL) - bias;
        long bits;
        if (delta >= -bias && delta <= bias) {
            previous_exp += delta;
            bits = in.readLong(1);
            bits = (bits << 11) | previous_exp;
            long segment = in.readLong(52);
            bits = (bits << 52) | segment;
            if (EL > 1) {
                int shrinkBias = DeXORTools.getP2(EL - 2) - 1;
                if (delta >= -shrinkBias && delta <= shrinkBias) {
                    contract_step++;
                } else {
                    contract_step = 0;
                }
                if (contract_step == rho) {
                    EL--;
                    contract_step = 0;
                }
            }
        } else {
            bits = in.readLong(64);
            previous_exp = DeXORTools.segment(bits, 2, 12);
            if (EL < 10) {
                EL++;
                contract_step = 0;
            }
        }
        return Double.longBitsToDouble(bits);
    }

    protected abstract class Method {
        protected double decodeDouble() {
            int control = in.readInt(2);
            if (control == 3) return ExceptionDecode();
            if (control == 0 || control == 1) {
                if (control == 0) previous_q = in.readInt(5) - 20;
                previous_delta = in.readInt(4);
                double pow = DeXORTools.getP10(previous_q + previous_delta);
                previous_alpha = DeXORTools.truncate(previous_value / pow) * pow;
            }
            long sign = previous_alpha > 0 ? 1 : -1;
            if (DeXORTools.comp(previous_alpha, 0) == 0) {
                sign = in.readBoolean() ? 1 : -1;
            }
            long betaStar = sign * in.readLong(DeXORTools.decimalBits(previous_delta));
            double beta = betaStar * DeXORTools.getP10(previous_q);
            previous_value = previous_alpha + beta;
            return previous_value;
        }
    }

    protected class Native extends Method {
    }

    protected class Buffered extends Method {
        protected int total = 0;

        @Override
        protected double decodeDouble() {
            int control = in.readInt(2);
            if (control == 3) return ExceptionDecode();
            int id = in.readInt(buffer_bits);
            previous_value = buffer[id];
            if (control == 0 || control == 1) {
                if (control == 0) previous_q = in.readInt(5) - 20;
                previous_delta = in.readInt(4);
            }
            double pow = DeXORTools.getP10(previous_q + previous_delta);
            previous_alpha = DeXORTools.truncate(previous_value / pow) * pow;
            long sign = previous_alpha > 0 ? 1 : -1;
            if (DeXORTools.comp(previous_alpha, 0) == 0) {
                sign = in.readBoolean() ? 1 : -1;
            }
            long beta = sign * in.readLong(DeXORTools.decimalBits(previous_delta));
            double residual = beta * DeXORTools.getP10(previous_q);
            previous_value = previous_alpha + residual;
            buffer[total++] = previous_value;
            total %= buffer.length;
            return previous_value;
        }
    }

    protected class Skippable extends Method {
        protected int exception_times = 0;

        @Override
        protected double decodeDouble() {
            if (skip) return ExceptionDecode();
            int control = in.readInt(2);
            if (control == 3) {
                exception_times++;
                if (exception_times >= skip_available) skip = true;
                return ExceptionDecode();
            }
            exception_times = 0;
            if (control == 0 || control == 1) {
                if (control == 0) previous_q = in.readInt(5) - 20;
                previous_delta = in.readInt(4);
                double pow = DeXORTools.getP10(previous_q + previous_delta);
                previous_alpha = DeXORTools.truncate(previous_value / pow) * pow;
            }
            long sign = previous_alpha > 0 ? 1 : -1;
            if (DeXORTools.comp(previous_alpha, 0) == 0) {
                sign = in.readBoolean() ? 1 : -1;
            }
            long betaStar = sign * in.readLong(DeXORTools.decimalBits(previous_delta));
            double beta = betaStar * DeXORTools.getP10(previous_q);
            previous_value = previous_alpha + beta;
            return previous_value;
        }
    }

    @Override
    public double decodeDouble() {
        return method.decodeDouble();
    }
}
