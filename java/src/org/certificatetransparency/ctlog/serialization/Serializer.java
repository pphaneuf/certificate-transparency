package org.certificatetransparency.ctlog.serialization;

import com.google.common.base.Preconditions;
import org.certificatetransparency.ctlog.SignedCertificateTimestamp;
import org.certificatetransparency.ctlog.proto.Ct;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.OutputStream;

/**
 * Serializes common structure to binary format.
 */
public class Serializer {
  /**
   * Write a numeric value of numBytes bytes, MSB first.
   * @param outputStream stream to write to.
   * @param value number to write. Must be non-negative.
   * @param numBytes number to bytes to write for the value.
   */
  public static void writeUint(OutputStream outputStream, long value, int numBytes) {
    Preconditions.checkArgument(value >= 0);
    Preconditions.checkArgument(value < Math.pow(256, numBytes),
        String.format("Value %d cannot be stored in %d bytes", value, numBytes));
    try {
      while (numBytes > 0) {
        // MSB first.
        int shiftBy = ((numBytes - 1) * 8);
        long mask = ((long) 0xff) << shiftBy;
        outputStream.write((byte) ((value & mask) >> shiftBy));
        numBytes--;
      }
    } catch (IOException e) {
      throw new SerializationException(
          String.format("Failure while writing number %d", value), e);
    }
  }

  /**
   * Write a variable-length array to the output stream.
   * @param outputStream stream to write to.
   * @param data data to write.
   * @param maxDataLength Maximal data length. Used for calculating the number of bytes needed
   *                      to store the length of the data.
   */
  public static void writeVariableLength(OutputStream outputStream,
                                         byte[] data, int maxDataLength) {
    Preconditions.checkArgument(data.length <= maxDataLength);
    int bytesForDataLength = Deserializer.bytesForDataLength(maxDataLength);
    writeUint(outputStream, data.length, bytesForDataLength);
    try {
      outputStream.write(data, 0, data.length);
    } catch (IOException e) {
      throw new SerializationException("Failure while writing byte array.", e);
    }
  }

  public static void writeFixedBytes(OutputStream outputStream, byte[] data) {
    try {
      outputStream.write(data);
    } catch (IOException e) {
      throw new SerializationException(
          String.format("Failure while writing fixed data buffer of length %d", data.length));
    }
  }

  public static byte[] serializeSctToBinary(SignedCertificateTimestamp sct) {
    ByteArrayOutputStream bos = new ByteArrayOutputStream();
    if (sct.version != Ct.Version.V1) {
      throw new SerializationException("Cannot serialize unknown SCT version: " + sct.version);
    }
    writeUint(bos, sct.version.getNumber(), CTConstants.VERSION_LENGTH);
    writeFixedBytes(bos, sct.id.getKeyId().toByteArray());
    writeUint(bos, sct.timestamp, CTConstants.TIMESTAMP_LENGTH);
    writeVariableLength(bos, sct.extensions, CTConstants.MAX_EXTENSIONS_LENGTH);
    writeUint(bos,
        sct.signature.getHashAlgorithm().getNumber(), CTConstants.HASH_ALG_LENGTH);
    writeUint(bos,
        sct.signature.getSigAlgorithm().getNumber(), CTConstants.SIGNATURE_ALG_LENGTH);
    writeVariableLength(bos,
        sct.signature.getSignature().toByteArray(), CTConstants.MAX_SIGNATURE_LENGTH);

    return bos.toByteArray();
  }
}
