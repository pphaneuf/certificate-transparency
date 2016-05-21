package org.certificatetransparency.ctlog;

import org.certificatetransparency.ctlog.proto.Ct;


public class SignedCertificateTimestamp {
  public Ct.Version version;
  public Ct.LogID id;
  // UTC time in milliseconds, since January 1, 1970, 00:00.
  public long timestamp;
  public Ct.DigitallySigned signature;
  public byte[] extensions;

  public SignedCertificateTimestamp(Ct.Version version_) {
    version = version_;
  }
}
