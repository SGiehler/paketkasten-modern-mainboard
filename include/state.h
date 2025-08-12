#ifndef STATE_H
#define STATE_H

enum MailboxState {
  LOCKED,
  OPENING_TO_PARCEL,
  PARCEL_OPEN,
  OPENING_TO_MAIL,
  MAIL_OPEN,
  LOCKING,
  MOTOR_ERROR
};

#endif
