# cloud

This Cloudflare Worker accepts authenticated batches from the indoor V1 ESP32 and serves a
public status page backed by D1. It remains optional during local plant validation. The
request contract is in `../SPEC.md` section 5. The public status page is a live summary, not
the authoritative archive for analysis releases.

## Local development

```bash
npm install
npx wrangler d1 migrations apply petrichor --local
npm test
npx wrangler dev
```

Local secrets belong in `.dev.vars`. Production uses `npx wrangler secret put CLOUD_SHARED_SECRET`.

## Deployment

```bash
npx wrangler d1 migrations apply petrichor --remote
npx wrangler deploy
```
