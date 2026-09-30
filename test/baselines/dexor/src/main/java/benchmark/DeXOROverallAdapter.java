package benchmark;

import algorithms.DeXOR.decoder.DoubleDeXORDecoder;
import algorithms.DeXOR.encoder.DoubleDeXOREncoder;

import java.io.BufferedReader;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/** Independent adapter for adding native DeXOR results to the FP64 overall CSVs. */
public final class DeXOROverallAdapter {
    private static final String METHOD = "DeXOR";
    private static final double[] DECIMAL_EPS = {
            1, 1e-1, 1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7,
            1e-8, 1e-9, 1e-10, 1e-11, 1e-12, 1e-13, 1e-14,
            1e-15, 1e-16, 1e-17, 1e-18, 1e-19, 1e-20, 1e-21,
            1e-22, 1e-23
    };
    private static final String[] DATASETS = {
            "Air-pressure.csv", "Basel-temp.csv", "Basel-wind.csv",
            "Chengdu-traj.csv", "City-temp.csv", "Dew-point-temp.csv",
            "IR-bio-temp.csv", "Motor-temp.csv", "PM10-dust.csv",
            "Smart-grid.csv", "Stocks-USA.csv", "T-drive.csv", "Wind-Speed.csv"
    };

    private DeXOROverallAdapter() {
    }

    private static final class Options {
        Path dataDir = Paths.get("test", "data_set");
        Path outputDir = Paths.get("test");
        Path mergeDir = null;
        int blockSize = 50;
        int runs = 3;
        int warmupBlocks = 400;
    }

    private static final class Metrics {
        final double compressionRatio;
        final double compressionMicrosPerBlock;
        final double decompressionMicrosPerBlock;
        final int blockCount;
        final long rawBitMismatches;
        final double maximumAbsoluteError;

        Metrics(double compressionRatio, double compressionMicrosPerBlock,
                double decompressionMicrosPerBlock, int blockCount,
                long rawBitMismatches, double maximumAbsoluteError) {
            this.compressionRatio = compressionRatio;
            this.compressionMicrosPerBlock = compressionMicrosPerBlock;
            this.decompressionMicrosPerBlock = decompressionMicrosPerBlock;
            this.blockCount = blockCount;
            this.rawBitMismatches = rawBitMismatches;
            this.maximumAbsoluteError = maximumAbsoluteError;
        }
    }

    private static final class PassResult {
        long compressedBits;
        long compressionNanos;
        long decompressionNanos;
        int blockCount;
        long rawBitMismatches;
        double maximumAbsoluteError;
    }

    public static void main(String[] args) throws Exception {
        Locale.setDefault(Locale.ROOT);
        Options options = parseOptions(args);
        Files.createDirectories(options.outputDir);

        Map<String, Metrics> results = new LinkedHashMap<>();
        for (String dataset : DATASETS) {
            double[] values = loadValues(options.dataDir.resolve(dataset));
            int completeBlocks = values.length / options.blockSize;
            if (completeBlocks == 0) {
                throw new IllegalArgumentException("Dataset has no complete block: " + dataset);
            }

            int warmupBlocks = Math.min(options.warmupBlocks, completeBlocks);
            if (warmupBlocks > 0) {
                runPass(values, options.blockSize, warmupBlocks, dataset, false);
            }

            long bits = 0;
            long compressionNanos = 0;
            long decompressionNanos = 0;
            long rawBitMismatches = 0;
            double maximumAbsoluteError = 0;
            for (int run = 0; run < options.runs; ++run) {
                PassResult pass = runPass(values, options.blockSize, completeBlocks,
                        dataset, true);
                bits += pass.compressedBits;
                compressionNanos += pass.compressionNanos;
                decompressionNanos += pass.decompressionNanos;
                if (run == 0) {
                    rawBitMismatches = pass.rawBitMismatches;
                    maximumAbsoluteError = pass.maximumAbsoluteError;
                }
            }

            double denominator = (double) options.runs * completeBlocks
                    * options.blockSize * 64.0;
            double timedBlocks = (double) options.runs * completeBlocks;
            Metrics metrics = new Metrics(
                    bits / denominator,
                    compressionNanos / 1000.0 / timedBlocks,
                    decompressionNanos / 1000.0 / timedBlocks,
                    completeBlocks, rawBitMismatches, maximumAbsoluteError);
            results.put(dataset, metrics);
            System.out.printf(Locale.ROOT,
                    "%s,%s,CR=%.6f,CT=%.6f,DT=%.6f,blocks=%d,raw-mismatches=%d,max-abs-error=%.17g%n",
                    METHOD, dataset, metrics.compressionRatio,
                    metrics.compressionMicrosPerBlock,
                    metrics.decompressionMicrosPerBlock, metrics.blockCount,
                    metrics.rawBitMismatches, metrics.maximumAbsoluteError);
        }

        String crRow = makeRow(results, MetricKind.CR);
        String ctRow = makeRow(results, MetricKind.CT);
        String dtRow = makeRow(results, MetricKind.DT);
        writeRow(options.outputDir.resolve("dexor_overall_cr.csv"), crRow);
        writeRow(options.outputDir.resolve("dexor_overall_ct.csv"), ctRow);
        writeRow(options.outputDir.resolve("dexor_overall_dt.csv"), dtRow);
        writeSummary(options.outputDir.resolve("dexor_overall_summary.csv"), results);

        if (options.mergeDir != null) {
            mergeRow(options.mergeDir.resolve("overall_cr_table.csv"), crRow);
            mergeRow(options.mergeDir.resolve("overall_ct_table.csv"), ctRow);
            mergeRow(options.mergeDir.resolve("overall_dt_table.csv"), dtRow);
        }
    }

    private static PassResult runPass(double[] values, int blockSize, int blockLimit,
                                      String dataset, boolean collectTiming) {
        DoubleDeXOREncoder encoder = new DoubleDeXOREncoder("");
        DoubleDeXORDecoder decoder = new DoubleDeXORDecoder("");
        PassResult result = new PassResult();

        for (int block = 0; block < blockLimit; ++block) {
            int offset = block * blockSize;
            long blockBits = 0;
            long compressionStart = System.nanoTime();
            for (int index = 0; index < blockSize; ++index) {
                blockBits += encoder.encode(values[offset + index]);
            }
            encoder.flush();
            long compressionEnd = System.nanoTime();
            byte[] compressed = encoder.getCompletedBlock();

            double[] reconstructed = new double[blockSize];
            long decompressionStart = System.nanoTime();
            decoder.setInput(compressed);
            for (int index = 0; index < blockSize; ++index) {
                reconstructed[index] = decoder.decodeDouble();
            }
            long decompressionEnd = System.nanoTime();

            for (int index = 0; index < blockSize; ++index) {
                long expected = Double.doubleToRawLongBits(values[offset + index]);
                long actual = Double.doubleToRawLongBits(reconstructed[index]);
                if (expected != actual) {
                    double absoluteError = Math.abs(values[offset + index] - reconstructed[index]);
                    if (!passesOfficialValidation(values[offset + index], reconstructed[index])) {
                        throw new IllegalStateException("DeXOR verification failed for "
                                + dataset + " at value " + (offset + index)
                                + ": expected=" + values[offset + index]
                                + ", actual=" + reconstructed[index]
                                + ", expected bits=" + Long.toUnsignedString(expected)
                                + ", actual bits=" + Long.toUnsignedString(actual));
                    }
                    ++result.rawBitMismatches;
                    result.maximumAbsoluteError = Math.max(result.maximumAbsoluteError,
                            absoluteError);
                }
            }

            result.compressedBits += blockBits;
            if (collectTiming) {
                result.compressionNanos += compressionEnd - compressionStart;
                result.decompressionNanos += decompressionEnd - decompressionStart;
            }
            ++result.blockCount;
        }
        return result;
    }

    private static boolean passesOfficialValidation(double expected, double actual) {
        int decimalPlaces = getDecimalPlaces(expected);
        return decimalPlaces >= 13
                || Math.abs(expected - actual) < DECIMAL_EPS[decimalPlaces];
    }

    private static int getDecimalPlaces(double value) {
        String text = Double.toString(value);
        int decimalPoint = text.indexOf('.');
        if (decimalPoint == -1) return 0;
        return Math.min(DECIMAL_EPS.length - 1,
                (text.length() - 1) - decimalPoint);
    }

    private static double[] loadValues(Path path) throws IOException {
        List<Double> values = new ArrayList<>();
        try (BufferedReader reader = Files.newBufferedReader(path, StandardCharsets.UTF_8)) {
            String line;
            while ((line = reader.readLine()) != null) {
                line = line.trim();
                if (!line.isEmpty()) values.add(Double.parseDouble(line));
            }
        }
        double[] result = new double[values.size()];
        for (int index = 0; index < values.size(); ++index) result[index] = values.get(index);
        return result;
    }

    private enum MetricKind { CR, CT, DT }

    private static String makeRow(Map<String, Metrics> results, MetricKind kind) {
        StringBuilder row = new StringBuilder(METHOD).append(',');
        for (String dataset : DATASETS) {
            Metrics metrics = results.get(dataset);
            double value;
            if (kind == MetricKind.CR) value = metrics.compressionRatio;
            else if (kind == MetricKind.CT) value = metrics.compressionMicrosPerBlock;
            else value = metrics.decompressionMicrosPerBlock;
            row.append(String.format(Locale.ROOT, "%.6f,", value));
        }
        return row.toString();
    }

    private static void writeRow(Path path, String row) throws IOException {
        Files.write(path, (row + System.lineSeparator()).getBytes(StandardCharsets.UTF_8));
    }

    private static void writeSummary(Path path, Map<String, Metrics> results) throws IOException {
        StringBuilder output = new StringBuilder("Method,DataSet,CompressionRatio,")
                .append("CompressionTime(us/block),DecompressionTime(us/block),")
                .append("RawBitMismatches,MaximumAbsoluteError\n");
        for (String dataset : DATASETS) {
            Metrics metrics = results.get(dataset);
            output.append(String.format(Locale.ROOT, "%s,%s,%.6f,%.6f,%.6f,%d,%.17g%n",
                    METHOD, dataset, metrics.compressionRatio,
                    metrics.compressionMicrosPerBlock,
                    metrics.decompressionMicrosPerBlock,
                    metrics.rawBitMismatches, metrics.maximumAbsoluteError));
        }
        Files.write(path, output.toString().getBytes(StandardCharsets.UTF_8));
    }

    private static void mergeRow(Path target, String row) throws IOException {
        if (!Files.exists(target)) {
            throw new IOException("Overall CSV does not exist: " + target);
        }
        List<String> input = Files.readAllLines(target, StandardCharsets.UTF_8);
        List<String> output = new ArrayList<>();
        boolean inserted = false;
        for (String line : input) {
            if (line.startsWith(METHOD + ",")) continue;
            output.add(line);
            if (line.startsWith("Elf,")) {
                output.add(row);
                inserted = true;
            }
        }
        if (!inserted) output.add(row);
        Path temporary = target.resolveSibling(target.getFileName() + ".dexor.tmp");
        Files.write(temporary, output, StandardCharsets.UTF_8);
        Files.move(temporary, target, StandardCopyOption.REPLACE_EXISTING);
    }

    private static Options parseOptions(String[] args) {
        Options options = new Options();
        for (int index = 0; index < args.length; ++index) {
            String argument = args[index];
            if ("--data-dir".equals(argument)) options.dataDir = Paths.get(requireValue(args, ++index, argument));
            else if ("--output-dir".equals(argument)) options.outputDir = Paths.get(requireValue(args, ++index, argument));
            else if ("--merge-dir".equals(argument)) options.mergeDir = Paths.get(requireValue(args, ++index, argument));
            else if ("--block-size".equals(argument)) options.blockSize = Integer.parseInt(requireValue(args, ++index, argument));
            else if ("--runs".equals(argument)) options.runs = Integer.parseInt(requireValue(args, ++index, argument));
            else if ("--warmup-blocks".equals(argument)) options.warmupBlocks = Integer.parseInt(requireValue(args, ++index, argument));
            else throw new IllegalArgumentException("Unknown argument: " + argument);
        }
        if (options.blockSize <= 0 || options.runs <= 0 || options.warmupBlocks < 0) {
            throw new IllegalArgumentException("Invalid benchmark configuration");
        }
        return options;
    }

    private static String requireValue(String[] args, int index, String option) {
        if (index >= args.length) throw new IllegalArgumentException("Missing value for " + option);
        return args[index];
    }
}
