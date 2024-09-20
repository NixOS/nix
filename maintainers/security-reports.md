# Handling security reports

Reports can be expected to be submitted following the [security policy](https://github.com/NixOS/nix/security/policy), but may reach maintainers on various other channels.

In case a vulnerability is reported:

1. [Create a GitHub security advisory](https://github.com/NixOS/nix/security/advisories/new)

   > [!IMPORTANT]
   > Add the reporter as a collaborator so they get notified of all activities.

   In addition to the details in the advisory template, the initial report should:
   
   - Include sufficient details of the vulnerability to allow it to be understood and reproduced.
   - Redact any personal data.
   - Set a deadline (if applicable).
   - Provide proof of concept code (if available).
   - Reference any further reading material that may be appropriate.

1. Establish a private communication channel (e.g. a Matrix room) with the reporter and all Nix maintainers.

1. Communicate with the reporter which team members are assigned and when they are available.

1. Consider which immediate preliminary measures should be taken before working on a fix.

1. Prioritize fixing the security issue over ongoing work.

1. Keep everyone involved up to date on progress and the estimated timeline for releasing the fix.

> See also the instructions for [security releases](./release-process.md#security-releases).

