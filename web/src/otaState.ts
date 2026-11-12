// What the running firmware's update state means, in words. A new image is checked after it
// starts and rolls itself back if its services are not up within two minutes.
export function otaStateText(state: string | undefined): string {
  switch (state) {
    case 'valid':
      return 'valid';
    case 'new':
    case 'pending_verify':
      return 'new firmware being checked (it rolls back if the check fails)';
    case 'invalid':
      return 'rejected by its health check';
    case 'aborted':
      return 'update aborted';
    default:
      return 'unknown';
  }
}
