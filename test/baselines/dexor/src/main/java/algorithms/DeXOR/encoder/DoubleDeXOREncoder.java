package algorithms.DeXOR.encoder;

import algorithms.Encoder;
import algorithms.DeXOR.DeXORTools;
import enums.DataTypeEnums;

/** Official DeXOR double encoder at upstream commit 06128b3. */
public class DoubleDeXOREncoder extends Encoder {
    protected int size = DataTypeEnums.DOUBLE.getSize();
    protected double previous_value = 0;
    protected int previous_q = 0;
    protected int previous_delta = 0;
    protected long previous_exp = 1023;
    protected int EL = 1;
    protected int contract_step = 0;
    protected boolean skip = false;
    protected Method method = new Native();
    protected int buffer_bits = 0;
    protected double[] buffer = new double[0];
    protected int rho = 8;
    protected int skip_available = -1;

    public DoubleDeXOREncoder(String outputPath) {
        super(outputPath);
    }

    public DoubleDeXOREncoder(String outputPath, String config) {
        super(outputPath, config);
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

    protected void ExceptionHandle(double value) {
        long bits = Double.doubleToRawLongBits(value);
        long exp = DeXORTools.segment(bits, 2, 12);
        long delta = exp - previous_exp;
        int bias = DeXORTools.getP2(EL - 1) - 1;
        if (delta >= -bias && delta <= bias) {
            out.write(delta + bias, EL);
            out.write(bits < 0);
            out.write(bits, 52);
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
            out.write(DeXORTools.getP2(EL) - 1, EL);
            out.write(bits, 64);
            contract_step = 0;
            if (EL < 10) EL++;
        }
        previous_exp = exp;
    }

    protected abstract class Method {
        protected void Decimal_XOR(double value) {
            int q = DeXORTools.getEnd(value, previous_q);
            int delta = 0;
            double alpha = 0;
            while (delta < 16) {
                double pow = DeXORTools.getP10(q + delta);
                long a = DeXORTools.truncate(value / pow);
                long b = DeXORTools.truncate(previous_value / pow);
                if (a == b) {
                    alpha = a * pow;
                    break;
                }
                delta++;
            }
            double pow = DeXORTools.getP10(q);
            double residual = value - alpha;
            long beta = Math.round(residual / pow);

            if (delta >= 16 || DeXORTools.comp(alpha + beta * pow, value, pow) != 0) {
                out.write(true);
                out.write(true);
                ExceptionHandle(value);
                return;
            }

            beta = Math.abs(beta);
            boolean sameQ = q == previous_q;
            if (sameQ && delta == previous_delta) {
                out.write(true);
                out.write(false);
            } else {
                out.write(false);
                out.write(sameQ);
                if (!sameQ) {
                    out.write(q + 20, 5);
                    previous_q = q;
                }
                out.write(delta, 4);
                previous_delta = delta;
            }
            if (DeXORTools.comp(alpha, 0) == 0) out.write(value > 0);
            out.write(beta, DeXORTools.decimalBits(delta));
            previous_value = value;
        }

        protected int encode(double value) {
            Decimal_XOR(value);
            return out.track_bits();
        }
    }

    protected class Native extends Method {
    }

    protected class Skippable extends Method {
        protected int exception_times = 0;

        @Override
        protected void Decimal_XOR(double value) {
            int q = DeXORTools.getEnd(value, previous_q);
            int delta = 0;
            double alpha = 0;
            while (delta < 16) {
                double pow = DeXORTools.getP10(q + delta);
                long a = DeXORTools.truncate(value / pow);
                long b = DeXORTools.truncate(previous_value / pow);
                if (a == b) {
                    alpha = a * pow;
                    break;
                }
                delta++;
            }
            double pow = DeXORTools.getP10(q);
            long beta = Math.round((value - alpha) / pow);
            if (delta >= 16 || DeXORTools.comp(alpha + beta * pow, value, pow) != 0) {
                out.write(true);
                out.write(true);
                exception_times++;
                if (exception_times >= skip_available) skip = true;
                ExceptionHandle(value);
                return;
            }
            exception_times = 0;
            beta = Math.abs(beta);
            boolean sameQ = q == previous_q;
            if (sameQ && delta == previous_delta) {
                out.write(true);
                out.write(false);
            } else {
                out.write(false);
                out.write(sameQ);
                if (!sameQ) {
                    out.write(q + 20, 5);
                    previous_q = q;
                }
                out.write(delta, 4);
                previous_delta = delta;
            }
            if (DeXORTools.comp(alpha, 0) == 0) out.write(value > 0);
            out.write(beta, DeXORTools.decimalBits(delta));
            previous_value = value;
        }

        @Override
        protected int encode(double value) {
            if (skip) ExceptionHandle(value);
            else Decimal_XOR(value);
            return out.track_bits();
        }
    }

    protected class Buffered extends Method {
        protected int total = 0;

        @Override
        protected void Decimal_XOR(double value) {
            int q = DeXORTools.getEnd(value, previous_q);
            int delta = 0;
            double alpha = 0;
            int id = 0;
            while (delta < 16) {
                double pow = DeXORTools.getP10(q + delta);
                long a = DeXORTools.truncate(value / pow);
                long b = DeXORTools.truncate(buffer[0] / pow);
                if (a == b) {
                    alpha = a * pow;
                    break;
                }
                delta++;
            }
            double pow = DeXORTools.getP10(q + delta - 1);
            long a = DeXORTools.truncate(value / pow);
            for (int i = 1; delta > 0 && i < buffer.length; i++) {
                long b = DeXORTools.truncate(buffer[i] / pow);
                while (delta > 0 && a == b) {
                    alpha = a * pow;
                    id = i;
                    delta--;
                    pow = DeXORTools.getP10(q + delta - 1);
                    a = DeXORTools.truncate(value / pow);
                    b = DeXORTools.truncate(buffer[i] / pow);
                }
            }
            double qPow = DeXORTools.getP10(q);
            long beta = Math.round((value - alpha) / qPow);
            if (delta >= 16 || DeXORTools.comp(alpha + beta * qPow, value, qPow) != 0) {
                out.write(true);
                out.write(true);
                ExceptionHandle(value);
                return;
            }
            beta = Math.abs(beta);
            boolean sameQ = q == previous_q;
            if (sameQ && delta == previous_delta) {
                out.write(true);
                out.write(false);
                out.write(id, buffer_bits);
            } else {
                out.write(false);
                out.write(sameQ);
                out.write(id, buffer_bits);
                if (!sameQ) {
                    out.write(q + 20, 5);
                    previous_q = q;
                }
                out.write(delta, 4);
                previous_delta = delta;
            }
            if (DeXORTools.comp(alpha, 0) == 0) out.write(value > 0);
            out.write(beta, DeXORTools.decimalBits(delta));
            buffer[total++] = value;
            total %= buffer.length;
        }
    }

    @Override
    public int encode(double value) {
        return method.encode(value);
    }
}
