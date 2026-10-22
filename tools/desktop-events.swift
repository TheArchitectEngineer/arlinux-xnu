// Deterministic external Apple-host window/input/pixel APIs. No OCR or AI.
import AppKit
import ApplicationServices
import ImageIO
import CryptoKit
let args=CommandLine.arguments
func application(_ index:Int) -> NSRunningApplication {
    guard args.count>index,let pid=Int32(args[index]),let app=NSRunningApplication(processIdentifier:pid) else { fatalError("Missing live app") }
    return app
}
func output(_ value:Any) throws { print(String(data:try JSONSerialization.data(withJSONObject:value),encoding:.utf8)!) }
switch args.count>1 ? args[1] : "" {
case "focus":
    for _ in 0..<40 {
        if let pid=Int32(args[2]),let app=NSRunningApplication(processIdentifier:pid),app.activate(options:[.activateIgnoringOtherApps]) { exit(0) }
        Thread.sleep(forTimeInterval:0.1)
    }
    fatalError("Cannot focus app")
case "close":
    guard application(2).terminate() else { fatalError("Normal termination rejected") }
case "bounds":
    guard args.count==3,let pid=Int32(args[2]) else { fatalError("Expected app PID") }
    let windows=CGWindowListCopyWindowInfo([.optionOnScreenOnly,.excludeDesktopElements],kCGNullWindowID) as? [[String:Any]] ?? []
    guard let window=windows.first(where:{ ($0[kCGWindowOwnerPID as String] as? Int32)==pid && ($0[kCGWindowLayer as String] as? Int)==0 }),
        let dictionary=window[kCGWindowBounds as String] as? [String:Any],
        let bounds=CGRect(dictionaryRepresentation:dictionary as CFDictionary) else {
        // UIKit state can precede WindowServer publication. Report absence;
        // the journey's existing deadline decides whether it ever appeared.
        print("null");break
    }
    let scale=Double(CGDisplayPixelsWide(CGMainDisplayID()))/CGDisplayBounds(CGMainDisplayID()).width
    try output([bounds.minX*scale,bounds.minY*scale,bounds.width*scale,bounds.height*scale])
case "accessibility", "press":
    guard args.count >= 3,let pid=Int32(args[2]),args[1] != "press" || args.count==4 else { fatalError("Expected PID and optional exact button title") }
    let trusted=AXIsProcessTrusted()
    var records=[[String:Any]](),target:AXUIElement?
    func attribute(_ element:AXUIElement,_ name:String) -> CFTypeRef? {
        var value:CFTypeRef?
        return AXUIElementCopyAttributeValue(element,name as CFString,&value) == .success ? value : nil
    }
    func visit(_ element:AXUIElement,_ depth:Int) {
        if depth>12 || records.count>=128 { return }
        let role=attribute(element,kAXRoleAttribute) as? String ?? ""
        let title=attribute(element,kAXTitleAttribute) as? String ?? ""
        let value=attribute(element,kAXValueAttribute) as? String ?? ""
        records.append(["role":role,"title":String(title.prefix(256)),"value":String(value.prefix(256)),"depth":depth])
        if args[1]=="press",role==(kAXButtonRole as String),title==args[3] {
            if target != nil { fatalError("Ambiguous external accessibility button") }
            target=element
        }
        for child in attribute(element,kAXChildrenAttribute) as? [AXUIElement] ?? [] { visit(child,depth+1) }
    }
    if trusted { visit(AXUIElementCreateApplication(pid),0) }
    var result:[String:Any]=["trusted":trusted,"elements":records,"limitReached":records.count>=128,"found":target != nil]
    if let target=target { result["actionError"]=AXUIElementPerformAction(target,kAXPressAction as CFString).rawValue }
    try output(result)
case "click":
    guard args.count==4,let x=Double(args[2]),let y=Double(args[3]) else { fatalError("Expected pixel coordinates") }
    let scale=Double(CGDisplayPixelsWide(CGMainDisplayID()))/CGDisplayBounds(CGMainDisplayID()).width
    for kind:CGEventType in [.mouseMoved,.leftMouseDown,.leftMouseUp] {
        let event=CGEvent(mouseEventSource:nil,mouseType:kind,mouseCursorPosition:CGPoint(x:x/scale,y:y/scale),mouseButton:.left)!
        event.post(tap:.cghidEventTap);Thread.sleep(forTimeInterval:0.08)
    }
case "sample":
    guard args.count==7,let source=CGImageSourceCreateWithURL(URL(fileURLWithPath:args[2]) as CFURL,nil),
        let image=CGImageSourceCreateImageAtIndex(source,0,nil),let x=Int(args[3]),let y=Int(args[4]),
        let width=Int(args[5]),let height=Int(args[6]),x>=0,y>=0,width>0,height>0,x+width<=image.width,y+height<=image.height,
        let crop=image.cropping(to:CGRect(x:x,y:y,width:width,height:height)) else { fatalError("Invalid actual screenshot region") }
    var bytes=[UInt8](repeating:0,count:width*height*4)
    bytes.withUnsafeMutableBytes { raw in
        let context=CGContext(data:raw.baseAddress,width:width,height:height,bitsPerComponent:8,bytesPerRow:width*4,
            space:CGColorSpaceCreateDeviceRGB(),bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue)!
        context.draw(crop,in:CGRect(x:0,y:0,width:width,height:height))
    }
    var ink=0,white=0,red=0,green=0
    var luminance=[Int](repeating:0,count:256)
    for i in stride(from:0,to:bytes.count,by:4) {
        let r=bytes[i],g=bytes[i+1],b=bytes[i+2]
        luminance[(Int(r)*19595+Int(g)*38470+Int(b)*7471+32768)>>16]+=1
        if min(r,g,b)<220 { ink+=1 }; if min(r,g,b)>240 { white+=1 }
        if r>200 && g<70 && b<70 { red+=1 };if g>200 && r<70 && b<70 { green+=1 }
    }
    let occupied=luminance.indices.filter{luminance[$0]>=8}
    let contrastSpan=occupied.isEmpty ? 0 : occupied.last!-occupied.first!
    try output(["pixels":width*height,"ink":ink,"white":white,"red":red,"green":green,"contrastSpan":contrastSpan,
                "sha256":SHA256.hash(data:Data(bytes)).map{String(format:"%02x",$0)}.joined()] as [String:Any])
case "type":
    guard args.count==3 else { fatalError("Expected text") }
    for character in args[2] {
        let units=Array(String(character).utf16)
        for down in [true,false] {
            let event=CGEvent(keyboardEventSource:nil,virtualKey:0,keyDown:down)!
            event.keyboardSetUnicodeString(stringLength:units.count,unicodeString:units);event.post(tap:.cghidEventTap)
        }
    }
default:fatalError("Expected focus, close, bounds, accessibility, press, click, sample or type")
}
