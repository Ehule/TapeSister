# Parent / Current model

TapeSister owns one immutable source buffer called **Parent** and one rendered buffer called **Current**.

```text
generated recipe or imported WAV
              |
           Parent
              |
    crop range plus processing
              |
           Current
```

Parent changes only when the user explicitly imports, generates, or reseeds a generated Parent. Body, Edge, Drift, selection, zoom, crop, audition, undo, redo, saving, and export do not replace Parent.

Crop is represented as a range into Parent rather than as a destructive rewrite. Undo history therefore stores compact edit state instead of copying large audio buffers. Current is deterministically rerendered from Parent and that state.

For a generated Parent, Reseed changes the generator seed while retaining its generator family. For an imported Parent, Reseed changes the processing seed and rerenders Current while the Parent hash remains identical.

Future destructive sample operations must either remain replayable edit operations or explicitly create a new committed Parent. They must never blur the Parent/Current boundary.
