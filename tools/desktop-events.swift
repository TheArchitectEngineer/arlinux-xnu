// External Apple-host desktop events only. Coordinates come from composed-screen OCR.
import AppKit
import ApplicationServices
import ImageIO
import Vision
let args=CommandLine.arguments
func application(_ index:Int) -> NSRunningApplication {
    guard args.count>index,let pid=Int32(args[index]),let app=NSRunningApplication(processIdentifier:pid) else { fatalError("Missing live app") }
    return app
}
switch args.count>1 ? args[1] : "" {
case "focus":
    for _ in 0..<40 {
        if let pid=Int32(args[2]),let app=NSRunningApplication(processIdentifier:pid),app.activate(options:[.activateIgnoringOtherApps]) { exit(0) }
        Thread.sleep(forTimeInterval:0.1)
    }
    fatalError("Cannot focus app")
case "close":
    guard application(2).terminate() else { fatalError("Normal termination rejected") }
case "click":
    guard args.count==4,let x=Double(args[2]),let y=Double(args[3]) else { fatalError("Expected pixel coordinates") }
    let display=CGMainDisplayID(),bounds=CGDisplayBounds(CGMainDisplayID())
    let scale=Double(CGDisplayPixelsWide(display))/bounds.width
    let point=CGPoint(x:x/scale,y:y/scale)
    for kind:CGEventType in [.mouseMoved,.leftMouseDown,.leftMouseUp] {
        guard let event=CGEvent(mouseEventSource:nil,mouseType:kind,mouseCursorPosition:point,mouseButton:.left) else { fatalError("Cannot create pointer event") }
        event.post(tap:.cghidEventTap);Thread.sleep(forTimeInterval:0.08)
    }
case "ocr":
    guard args.count==3,let source=CGImageSourceCreateWithURL(URL(fileURLWithPath:args[2]) as CFURL,nil),
        let image=CGImageSourceCreateImageAtIndex(source,0,nil) else { fatalError("Missing composed screenshot") }
    let request=VNRecognizeTextRequest();request.recognitionLevel = .accurate;request.recognitionLanguages=["en-US"]
    try VNImageRequestHandler(cgImage:image).perform([request])
    let words:[[String:Any]]=(request.results ?? []).compactMap { observation in
        guard let text=observation.topCandidates(1).first else { return nil }
        let box=observation.boundingBox
        return ["text":text.string,"left":box.minX*Double(image.width),"top":(1-box.maxY)*Double(image.height),
            "width":box.width*Double(image.width),"height":box.height*Double(image.height),"confidence":text.confidence]
    }
    print(String(data:try JSONSerialization.data(withJSONObject:words),encoding:.utf8)!)
case "pixels":
    guard args.count==4,let source=CGImageSourceCreateWithURL(URL(fileURLWithPath:args[2]) as CFURL,nil),
        let image=CGImageSourceCreateImageAtIndex(source,0,nil) else { fatalError("Missing composed screenshot") }
    let width=image.width,height=image.height
    var bytes=[UInt8](repeating:0,count:width*height*4)
    let count=bytes.withUnsafeMutableBytes { raw -> Int in
        let context=CGContext(data:raw.baseAddress,width:width,height:height,bitsPerComponent:8,bytesPerRow:width*4,
            space:CGColorSpaceCreateDeviceRGB(),bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue)!
        context.draw(image,in:CGRect(x:0,y:0,width:width,height:height))
        let pointer=raw.bindMemory(to:UInt8.self); let channel=args[3]=="red" ? 0 : 1
        return stride(from:0,to:pointer.count,by:4).filter { i in
            pointer[i+channel]>200 && (0..<3).allSatisfy { $0==channel || pointer[i+$0]<70 }
        }.count
    }
    print(count)
case "type":
    guard args.count==3 else { fatalError("Expected text") }
    for character in args[2] {
        let units=Array(String(character).utf16)
        for down in [true,false] {
            let event=CGEvent(keyboardEventSource:nil,virtualKey:0,keyDown:down)!
            event.keyboardSetUnicodeString(stringLength:units.count,unicodeString:units);event.post(tap:.cghidEventTap)
        }
    }
default:fatalError("Expected focus, close, click or type")
}
