package com.cruzdb;

import java.util.Random;

import static org.junit.Assert.*;
import org.junit.*;
import static org.assertj.core.api.Assertions.assertThat;

public class LogTest {

  // LogException is thrown when the Log cannot be created
  //@Test(expected=LogException.class)
  //public void openThrows() throws LogException {
  //  Random rand = new Random();
  //  String logname = "" + rand.nextInt();
  //  Log log = Log.openLMDB(logname);
  //  //Log log = Log.openLMDB("xyz", "abc", 5678, logname);
  //}

  @Test(expected=NullPointerException.class)
  public void appendNullAppend() throws LogException {
    Random rand = new Random();
    String logname = "" + rand.nextInt();
    Log log = Log.openLMDB(logname);
    //Log log = Log.openLMDB("rbd", "localhost", 5678, logname);
    log.append(null);
  }

  @Test
  public void append() throws LogException {
    Random rand = new Random();
    String logname = "" + rand.nextInt();
    Log log = Log.openLMDB(logname);
    //Log log = Log.openLMDB("rbd", "localhost", 5678, logname);
    long first_pos = log.append(new byte[20]);
    long pos = log.append(new byte[20]);
    assertEquals(pos, first_pos+1);
    pos = log.append(new byte[20]);
    assertEquals(pos, first_pos+2);
  }

  @Test(expected=NotWrittenException.class)
  public void readNotWritten() throws LogException {
    Random rand = new Random();
    String logname = "" + rand.nextInt();
    Log log = Log.openLMDB(logname);
    //Log log = Log.openLMDB("rbd", "localhost", 5678, logname);
    log.read(20);
  }

  @Test(expected=FilledException.class)
  public void readFilled() throws LogException {
    Random rand = new Random();
    String logname = "" + rand.nextInt();
    Log log = Log.openLMDB(logname);
    //Log log = Log.openLMDB("rbd", "localhost", 5678, logname);
    log.fill(20);
    log.read(20);
  }

  @Test
  public void read() throws LogException {
    Random rand = new Random();
    String logname = "" + rand.nextInt();
    Log log = Log.openLMDB(logname);
    //Log log = Log.openLMDB("rbd", "localhost", 5678, logname);
    byte[] indata = "this is the input".getBytes();
    long pos = log.append(indata);
    byte[] outdata = log.read(pos);
    assertArrayEquals(indata, outdata);
  }

  @Test(expected=ReadOnlyException.class)
  public void fillReadOnly() throws LogException {
    Random rand = new Random();
    String logname = "" + rand.nextInt();
    Log log = Log.openLMDB(logname);
    //Log log = Log.openLMDB("rbd", "localhost", 5678, logname);
    long pos = log.append("asdf".getBytes());
    log.fill(pos);
  }

  @Test
  public void fill() throws LogException {
    Random rand = new Random();
    String logname = "" + rand.nextInt();
    Log log = Log.openLMDB(logname);
    //Log log = Log.openLMDB("rbd", "localhost", 5678, logname);
    log.fill(33);
  }

  @Test
  public void trim() throws LogException {
    Random rand = new Random();
    String logname = "" + rand.nextInt();
    Log log = Log.openLMDB(logname);
    //Log log = Log.openLMDB("rbd", "localhost", 5678, logname);
    log.trim(33);
    long pos = log.append("asdf".getBytes());
    log.trim(pos);
  }

  @Test
  public void tail() throws LogException {
    Random rand = new Random();
    String logname = "" + rand.nextInt();
    Log log = Log.openLMDB(logname);
    //Log log = Log.openLMDB("rbd", "localhost", 5678, logname);
    long pos = log.tail();
    assertEquals(pos, 0);
    pos = log.tail();
    assertEquals(pos, 0);
    long pos2 = log.append("asdf".getBytes());
    pos = log.tail();
    assertEquals(pos, pos2+1);
  }

  @Test
  public void dbOpen() throws LogException {
    Random rand = new Random();
    String logname = "" + rand.nextInt();
    Log log = Log.openLMDB(logname);

    DB db = DB.open(log, true);
  }

  @Test
  public void put() throws LogException {
    Random rand = new Random();
    String logname = "" + rand.nextInt();
    Log log = Log.openLMDB(logname);

    DB db = DB.open(log, true);
    db.put("key1".getBytes(), "value".getBytes());
    assertArrayEquals(db.get("key1".getBytes()), "value".getBytes());

    db.delete("key1".getBytes());
  }

  @Test
  public void iterator() throws LogException {
    Random rand = new Random();
    String logname = "" + rand.nextInt();
    Log log = Log.openLMDB(logname);

    DB db = DB.open(log, true);
    db.put("key1".getBytes(), "value1".getBytes());
    db.put("key2".getBytes(), "value2".getBytes());

    CruzIterator iterator = db.newIterator();
    iterator.seekToFirst();
    assertThat(iterator.isValid()).isTrue();
    assertThat(iterator.key()).isEqualTo("key1".getBytes());
    assertThat(iterator.value()).isEqualTo("value1".getBytes());
    iterator.next();
    assertThat(iterator.isValid()).isTrue();
    assertThat(iterator.key()).isEqualTo("key2".getBytes());
    assertThat(iterator.value()).isEqualTo("value2".getBytes());
    iterator.next();
    assertThat(iterator.isValid()).isFalse();
    iterator.seekToLast();
    iterator.prev();
    assertThat(iterator.isValid()).isTrue();
    assertThat(iterator.key()).isEqualTo("key1".getBytes());
    assertThat(iterator.value()).isEqualTo("value1".getBytes());
    iterator.seekToFirst();
    iterator.seekToLast();
    assertThat(iterator.isValid()).isTrue();
    assertThat(iterator.key()).isEqualTo("key2".getBytes());
    assertThat(iterator.value()).isEqualTo("value2".getBytes());
    iterator.next();
    assertThat(iterator.isValid()).isFalse();
  }

  @Test
  public void txn() throws LogException {
    Random rand = new Random();
    String logname = "" + rand.nextInt();
    Log log = Log.openLMDB(logname);

    DB db = DB.open(log, true);
    db.put("key1".getBytes(), "value1".getBytes());
    db.put("key2".getBytes(), "value2".getBytes());

    Transaction txn = db.newTransaction();
    byte[] value = txn.get("key1".getBytes());
    txn.commit();
  }
}